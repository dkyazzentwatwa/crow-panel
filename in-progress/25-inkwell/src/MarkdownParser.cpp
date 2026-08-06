#include "MarkdownParser.h"

namespace {

void emit(std::vector<Ink::Run> &runs, std::string &buf, bool b, bool i, bool m) {
  if (buf.empty()) return;
  runs.push_back({buf, b, i, m});
  buf.clear();
}

// Inline markdown -> runs. Emphasis must close within the block or it is
// emitted literally (testMdEdges pins this).
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
      if (!bold && s.find("**", i + 2) == std::string::npos) { buf += "**"; i += 2; continue; }
      emit(runs, buf, bold, italic, false); bold = !bold; i += 2; continue;
    }
    if (s[i] == '*' || s[i] == '_') {
      char c = s[i];
      if (!italic && s.find(c, i + 1) == std::string::npos) { buf += c; ++i; continue; }
      emit(runs, buf, bold, italic, false); italic = !italic; ++i; continue;
    }
    if (s[i] == '!' && i + 1 < s.size() && s[i + 1] == '[') {  // image: drop
      size_t close = s.find(']', i);
      size_t paren = close == std::string::npos ? std::string::npos : s.find(')', close);
      if (paren != std::string::npos) { i = paren + 1; continue; }
    }
    if (s[i] == '[') {  // link: keep text, drop target
      size_t close = s.find("](", i);
      size_t paren = close == std::string::npos ? std::string::npos : s.find(')', close);
      if (paren != std::string::npos) {
        std::string inner = s.substr(i + 1, close - i - 1);
        for (const Ink::Run &r : inlineRuns(inner))
          { emit(runs, buf, bold, italic, false); runs.push_back(r); }
        i = paren + 1; continue;
      }
    }
    if (s[i] == '<') {  // strip HTML tags, keep text between them
      size_t close = s.find('>', i);
      if (close != std::string::npos && close - i < 64) { i = close + 1; continue; }
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
std::string normalizeLine(const std::string &raw) {
  std::string line;
  for (char ch : raw) {
    char c = (ch == '\t') ? ' ' : ch;
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
// nesting capped at depth 3.
bool isListLine(const std::string &line, bool &ordered, uint8_t &depth, std::string &content) {
  size_t indent = 0;
  while (indent < line.size() && line[indent] == ' ') ++indent;
  size_t p = indent;
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

    // Paragraph: consecutive plain lines join with a single space.
    size_t offset = lineStart + firstNonBlank(line);
    std::string text = normalizeLine(line);
    i = nextI;
    while (i < n) {
      size_t ls, next2;
      std::string nl = readLine(src, i, ls, next2);
      std::string nn = normalizeLine(nl);
      if (nn.empty()) break;
      bool dOrdered;
      uint8_t dDepth;
      std::string dContent, dQuote;
      if (isFenceLine(nl) || headingLevel(nl, dContent) > 0 || isRuleLine(nl) ||
          isQuoteLine(nl, dQuote) || isListLine(nl, dOrdered, dDepth, dContent)) {
        break;
      }
      text += ' ';
      text += nn;
      i = next2;
    }
    blocks.push_back(makeBlock(BlockType::Body, offset, text));
  }
  return blocks;
}

}  // namespace Ink
