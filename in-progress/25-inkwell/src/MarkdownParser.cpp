#include "MarkdownParser.h"

#include <algorithm>
#include <cctype>

namespace {

// Window bounds for the link/image/tag lookaheads below. Without a cap, a
// pathological run of unmatched '[' or '![' markers (no ']'/')' anywhere
// later in the block) turns each marker's find() into an O(n) scan of the
// remaining text, making the whole block O(n^2) -- measured ~18s for a
// 512KB single-block input, which would trip the ESP32 task watchdog.
// Bounding every scan to a fixed window keeps the block linear: each marker
// costs at most kLinkWindow/kTagWindow work regardless of block size.
constexpr size_t kLinkWindow = 512;
constexpr size_t kTagWindow = 64;

size_t findCharBounded(const std::string &s, char target, size_t from, size_t limit) {
  size_t end = std::min(s.size(), limit);
  for (size_t j = from; j < end; ++j)
    if (s[j] == target) return j;
  return std::string::npos;
}

// Finds the two-char sequence {a, b} within [from, limit).
size_t findPairBounded(const std::string &s, char a, char b, size_t from, size_t limit) {
  size_t end = std::min(s.size(), limit);
  for (size_t j = from; j + 1 < end; ++j)
    if (s[j] == a && s[j + 1] == b) return j;
  return std::string::npos;
}

void emit(std::vector<Ink::Run> &runs, std::string &buf, bool bold, bool italic, bool mono) {
  if (buf.empty()) return;
  runs.push_back({buf, bold, italic, mono});
  buf.clear();
}

// Inline markdown -> runs. An opener with no closer marker ahead in the
// block is emitted literally (testMdEdges pins this); the flanking checks
// below can also force a literal emission even when a closer does exist
// (e.g. "5 * 3", "snake_case_name").
std::vector<Ink::Run> inlineRuns(const std::string &s) {
  std::vector<Ink::Run> runs;
  std::string buf;
  bool bold = false, italic = false, mono = false;
  for (size_t i = 0; i < s.size();) {
    if (mono) {  // inside `...`: only a backtick ends it
      if (s[i] == '`') { emit(runs, buf, bold, italic, true); mono = false; ++i; }
      else buf += s[i++];
      continue;
    }
    if (s[i] == '`') {
      if (s.find('`', i + 1) == std::string::npos) { buf += '`'; ++i; continue; }
      emit(runs, buf, bold, italic, false); mono = true; ++i; continue;
    }
    if (s.compare(i, 2, "**") == 0) {
      // Flanking rules keep "5 * 3" style prose literal: an opener needs a
      // non-space right after it, a closer needs a non-space right before.
      if (!bold) {
        bool noCloser = s.find("**", i + 2) == std::string::npos;
        bool afterIsSpace = i + 2 >= s.size() || s[i + 2] == ' ';
        if (noCloser || afterIsSpace) { buf += "**"; i += 2; continue; }
      } else {
        bool beforeIsSpace = i == 0 || s[i - 1] == ' ';
        if (beforeIsSpace) { buf += "**"; i += 2; continue; }
      }
      emit(runs, buf, bold, italic, false); bold = !bold; i += 2; continue;
    }
    if (s[i] == '*' || s[i] == '_') {
      char c = s[i];
      bool prevAlnum = i > 0 && std::isalnum((unsigned char)s[i - 1]);
      bool nextAlnum = i + 1 < s.size() && std::isalnum((unsigned char)s[i + 1]);
      // Underscore only: intra-word markers (snake_case_name) never toggle.
      bool intraWordUnderscore = c == '_' && prevAlnum && nextAlnum;
      if (!italic) {
        bool noCloser = s.find(c, i + 1) == std::string::npos;
        bool afterIsSpace = i + 1 >= s.size() || s[i + 1] == ' ';
        if (noCloser || afterIsSpace || intraWordUnderscore) { buf += c; ++i; continue; }
      } else {
        bool beforeIsSpace = i == 0 || s[i - 1] == ' ';
        if (beforeIsSpace || intraWordUnderscore) { buf += c; ++i; continue; }
      }
      emit(runs, buf, bold, italic, false); italic = !italic; ++i; continue;
    }
    if (s[i] == '!' && i + 1 < s.size() && s[i + 1] == '[') {  // image: drop
      size_t limit = i + kLinkWindow;
      size_t close = findCharBounded(s, ']', i, limit);
      size_t paren = close == std::string::npos ? std::string::npos
                                                 : findCharBounded(s, ')', close, limit);
      if (paren != std::string::npos) { i = paren + 1; continue; }
    }
    if (s[i] == '[') {
      // Link: keep text, drop target. Recursion depth is bounded at 2:
      // `close` is the FIRST "](" at or after `i`, so `inner` (the slice
      // strictly before `close`) provably contains no "](" of its own --
      // any '[' nested inside `inner` therefore always falls through to a
      // literal '[' on the recursive call, it can never open a second real
      // link. Do not upgrade this to balanced-bracket matching without also
      // adding an explicit depth cap.
      size_t limit = i + kLinkWindow;
      size_t close = findPairBounded(s, ']', '(', i, limit);
      size_t paren = close == std::string::npos ? std::string::npos
                                                 : findCharBounded(s, ')', close, limit);
      if (paren != std::string::npos) {
        std::string inner = s.substr(i + 1, close - i - 1);
        for (const Ink::Run &r : inlineRuns(inner)) {
          emit(runs, buf, bold, italic, false);
          // The link text renders inside whatever style already surrounds
          // the link markup (e.g. "**bold [link](u)**" keeps the link
          // bold), so OR the enclosing state into each recursed run.
          Ink::Run merged = r;
          merged.bold = merged.bold || bold;
          merged.italic = merged.italic || italic;
          merged.mono = merged.mono || mono;
          runs.push_back(merged);
        }
        i = paren + 1; continue;
      }
    }
    if (s[i] == '<') {  // strip HTML tags, keep text between them
      size_t limit = i + kTagWindow;
      size_t close = findCharBounded(s, '>', i, limit);
      if (close != std::string::npos) { i = close + 1; continue; }
    }
    buf += s[i++];
  }
  emit(runs, buf, bold, italic, mono);
  if (runs.empty()) runs.push_back({"", false, false, false});
  return runs;
}

}  // namespace

namespace Ink {

namespace {

// Reads the line starting at src[i], normalizing CRLF to LF. Sets lineStart
// (== i, the byte offset the line begins at) and nextI (index of the byte
// after this line, i.e. where the next line -- or EOF -- begins).
std::string readLine(const std::string &src, size_t i, size_t &lineStart, size_t &nextI) {
  size_t n = src.size();
  lineStart = i;
  size_t eol = src.find('\n', i);
  size_t end = (eol == std::string::npos) ? n : eol;
  size_t trimEnd = end;
  if (trimEnd > lineStart && src[trimEnd - 1] == '\r') --trimEnd;
  nextI = (eol == std::string::npos) ? n : eol + 1;
  return src.substr(lineStart, trimEnd - lineStart);
}

// Trims leading/trailing spaces and tabs and collapses internal runs of
// whitespace to a single space -- mirrors TxtParser's per-line handling.
// A lone (non-CRLF) '\r' maps to a space exactly like TxtParser.cpp does,
// so stray old-Mac-style carriage returns never leak into rendered text.
std::string normalizeLine(const std::string &raw) {
  std::string line;
  for (char ch : raw) {
    char c = (ch == '\t' || ch == '\r') ? ' ' : ch;
    if (c == ' ' && !line.empty() && line.back() == ' ') continue;
    line += c;
  }
  while (!line.empty() && line.back() == ' ') line.pop_back();
  size_t first = line.find_first_not_of(' ');
  return first == std::string::npos ? "" : line.substr(first);
}

size_t firstNonBlank(const std::string &s) {
  size_t p = s.find_first_not_of(" \t");
  return p == std::string::npos ? 0 : p;
}

bool isFenceLine(const std::string &line) {
  size_t p = line.find_first_not_of(" \t");
  if (p == std::string::npos) return false;
  return line.compare(p, 3, "```") == 0;
}

// Heading level 1-3 (4+ '#' collapses to 3), or 0 if not a heading. Content
// is everything after the marker's single required space.
int headingLevel(const std::string &line, std::string &content) {
  size_t p = line.find_first_not_of(" \t");
  if (p == std::string::npos) return 0;
  size_t h = p;
  while (h < line.size() && line[h] == '#') ++h;
  int count = (int)(h - p);
  if (count < 1 || count > 6) return 0;
  if (h >= line.size() || line[h] != ' ') return 0;
  content = line.substr(h + 1);
  return count > 3 ? 3 : count;
}

// 3+ of the same '-'/'*'/'_' character, optionally spaced apart.
bool isRuleLine(const std::string &line) {
  std::string stripped;
  for (char c : line)
    if (c != ' ' && c != '\t') stripped += c;
  if (stripped.size() < 3) return false;
  char c0 = stripped[0];
  if (c0 != '-' && c0 != '*' && c0 != '_') return false;
  for (char c : stripped)
    if (c != c0) return false;
  return true;
}

bool isQuoteLine(const std::string &line, std::string &content) {
  size_t p = line.find_first_not_of(" \t");
  if (p == std::string::npos || line[p] != '>') return false;
  size_t rest = p + 1;
  if (rest < line.size() && line[rest] == ' ') ++rest;
  content = line.substr(rest);
  return true;
}

// "- "/"* " (unordered) or "\d+. " (ordered), with 2-space-per-level
// nesting capped at depth 3. Each leading tab counts as 2 spaces of indent.
bool isListLine(const std::string &line, bool &ordered, uint8_t &depth, std::string &content) {
  size_t indent = 0;
  size_t p = 0;
  while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) {
    indent += line[p] == '\t' ? 2 : 1;
    ++p;
  }
  if (p < line.size() && (line[p] == '-' || line[p] == '*') && p + 1 < line.size() &&
      line[p + 1] == ' ') {
    ordered = false;
    content = line.substr(p + 2);
  } else {
    size_t d = p;
    while (d < line.size() && line[d] >= '0' && line[d] <= '9') ++d;
    if (d == p || d + 1 >= line.size() || line[d] != '.' || line[d + 1] != ' ') return false;
    ordered = true;
    content = line.substr(d + 2);
  }
  size_t depthCalc = 1 + indent / 2;
  depth = (uint8_t)(depthCalc > 3 ? 3 : depthCalc);
  return true;
}

// True if `line` opens a fence/heading/rule/quote/list block rather than
// plain paragraph text. Single source of truth for that classification --
// shared by the main dispatch chain and the paragraph-continuation
// lookahead so the same five-way test never has to be duplicated.
bool startsBlock(const std::string &line) {
  bool dOrdered = false;
  uint8_t dDepth = 0;
  std::string dContent, dQuote;
  return isFenceLine(line) || headingLevel(line, dContent) > 0 || isRuleLine(line) ||
         isQuoteLine(line, dQuote) || isListLine(line, dOrdered, dDepth, dContent);
}

// Runs the inline pass and trims a trailing whitespace-only tail run --
// dropping a trailing image/link/tag can leave a bare space behind that a
// styled block never intended to render (testMdEdges pins this).
Block makeBlock(BlockType type, size_t offset, const std::string &rawText) {
  Block b;
  b.type = type;
  b.srcOffset = (uint32_t)offset;
  b.runs = inlineRuns(rawText);
  while (!b.runs.empty()) {
    Run &last = b.runs.back();
    size_t end = last.text.find_last_not_of(' ');
    if (end == std::string::npos) { b.runs.pop_back(); continue; }
    if (end + 1 < last.text.size()) last.text.erase(end + 1);
    break;
  }
  // Preserve inlineRuns' >=1-run guarantee: trimming can legitimately empty
  // every run (e.g. a lone image/link/tag line), and downstream code
  // (styleFor, renderers) expects at least one run to exist.
  if (b.runs.empty()) b.runs.push_back({"", false, false, false});
  return b;
}

}  // namespace

std::vector<Block> parseMarkdown(const std::string &src) {
  std::vector<Block> blocks;
  size_t i = 0, n = src.size();
  while (i < n) {
    size_t lineStart, nextI;
    std::string line = readLine(src, i, lineStart, nextI);

    if (normalizeLine(line).empty()) { i = nextI; continue; }

    if (startsBlock(line)) {
      if (isFenceLine(line)) {
        size_t offset = lineStart + firstNonBlank(line);
        i = nextI;
        std::string codeText;
        bool first = true;
        while (i < n) {
          size_t ls, next2;
          std::string codeLine = readLine(src, i, ls, next2);
          if (isFenceLine(codeLine)) { i = next2; break; }
          if (!first) codeText += '\n';
          codeText += codeLine;
          first = false;
          i = next2;
        }
        Block b;
        b.type = BlockType::Code;
        b.srcOffset = (uint32_t)offset;
        b.runs.push_back({codeText, false, false, true});
        blocks.push_back(std::move(b));
        continue;
      }

      std::string headingContent;
      int level = headingLevel(line, headingContent);
      if (level > 0) {
        size_t offset = lineStart + firstNonBlank(line);
        BlockType t = level == 1 ? BlockType::H1 : level == 2 ? BlockType::H2 : BlockType::H3;
        blocks.push_back(makeBlock(t, offset, normalizeLine(headingContent)));
        i = nextI;
        continue;
      }

      if (isRuleLine(line)) {
        Block b;
        b.type = BlockType::Rule;
        b.srcOffset = (uint32_t)(lineStart + firstNonBlank(line));
        blocks.push_back(std::move(b));
        i = nextI;
        continue;
      }

      std::string quoteContent;
      if (isQuoteLine(line, quoteContent)) {
        size_t offset = lineStart + firstNonBlank(line);
        std::string text = normalizeLine(quoteContent);
        i = nextI;
        while (i < n) {
          size_t ls, next2;
          std::string nl = readLine(src, i, ls, next2);
          std::string qc;
          if (!isQuoteLine(nl, qc)) break;
          std::string nc = normalizeLine(qc);
          if (!nc.empty()) {
            if (!text.empty()) text += ' ';
            text += nc;
          }
          i = next2;
        }
        blocks.push_back(makeBlock(BlockType::Quote, offset, text));
        continue;
      }

      bool ordered = false;
      uint8_t depth = 0;
      std::string listContent;
      if (isListLine(line, ordered, depth, listContent)) {
        size_t offset = lineStart + firstNonBlank(line);
        Block b = makeBlock(BlockType::ListItem, offset, normalizeLine(listContent));
        b.ordered = ordered;
        b.listDepth = depth;
        blocks.push_back(std::move(b));
        i = nextI;
        continue;
      }
    }

    // Paragraph: consecutive plain lines join with a single space.
    size_t offset = lineStart + firstNonBlank(line);
    std::string text = normalizeLine(line);
    i = nextI;
    while (i < n) {
      size_t ls, next2;
      std::string nl = readLine(src, i, ls, next2);
      std::string nn = normalizeLine(nl);
      if (nn.empty() || startsBlock(nl)) break;
      text += ' ';
      text += nn;
      i = next2;
    }
    blocks.push_back(makeBlock(BlockType::Body, offset, text));
  }
  return blocks;
}

}  // namespace Ink
