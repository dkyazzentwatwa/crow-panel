#include "XhtmlParser.h"

#include <algorithm>
#include <cctype>

namespace Ink {

namespace {

// Window bounds for the bounded scans below -- see MarkdownParser.cpp for
// why these must be bounded rather than an unbounded find(): a pathological
// unmatched '<' or '&' with no '>' / ';' anywhere later in the input would
// otherwise turn every character's lookahead into an O(n) scan, making the
// whole document O(n^2).
constexpr size_t kTagWindow = 512;     // bytes scanned ahead of '<' for '>'
constexpr size_t kEntityMaxName = 12;  // max chars between '&' and ';'

size_t findCharBounded(const std::string &s, char target, size_t from, size_t limit) {
  size_t end = std::min(s.size(), limit);
  for (size_t j = from; j < end; ++j)
    if (s[j] == target) return j;
  return std::string::npos;
}

bool isWsChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string lowerStr(const std::string &s) {
  std::string out(s.size(), '\0');
  for (size_t i = 0; i < s.size(); ++i) out[i] = (char)std::tolower((unsigned char)s[i]);
  return out;
}

// Appends the UTF-8 encoding of code point `cp`. Code points above 0xFFFF
// (outside the range InkDoc's rendering pipeline is sized for) emit '?'
// per the documented degrade.
void appendUtf8(std::string &out, unsigned long cp) {
  if (cp <= 0x7F) {
    out += (char)cp;
  } else if (cp <= 0x7FF) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += '?';
  }
}

// Finds the index right after <body ...>'s '>', or 0 if no <body> tag
// exists anywhere -- the whole input is then parsed as-is. A single
// forward pass over '<' occurrences; the extra find('>') fires at most
// once (only once a body tag is actually spotted), so this stays O(n)
// overall rather than O(n^2).
size_t findBodyStart(const std::string &src) {
  size_t n = src.size();
  size_t i = 0;
  while (i < n) {
    size_t lt = src.find('<', i);
    if (lt == std::string::npos) return 0;
    size_t j = lt + 1;
    if (j < n && src[j] == '/') { i = j; continue; }
    size_t nameStart = j;
    while (j < n && std::isalnum((unsigned char)src[j])) ++j;
    if (lowerStr(src.substr(nameStart, j - nameStart)) == "body") {
      size_t gt = src.find('>', j);
      return gt == std::string::npos ? n : gt + 1;
    }
    i = lt + 1;
  }
  return 0;
}

struct ListEntry {
  bool ordered;
};

// Accumulates one pending block's runs as the forward scan finds text and
// inline-style tags, and flushes finished blocks into `blocks`. Mirrors
// MarkdownParser's buf/emit idiom, but driven by tag events instead of
// line boundaries.
class Builder {
 public:
  explicit Builder(std::vector<Block> &blocks) : blocks_(blocks) {}

  void openBlock(BlockType type, uint32_t offset, uint8_t listDepth = 0, bool ordered = false) {
    if (blockOpen_) flush();
    type_ = type;
    offset_ = offset;
    listDepth_ = listDepth;
    ordered_ = ordered;
    blockOpen_ = true;
    blockHasContent_ = false;
    spacePending_ = false;
    buf_.clear();
    runs_.clear();
    runBold_ = runItalic_ = runMono_ = false;
  }

  // Flushes whatever is pending, so a completed block's runs land in
  // `blocks` in source order before this is called.
  void flush() {
    if (!blockOpen_) return;
    flushBuf();
    // Zero-run guard convention (MarkdownParser.cpp): every non-Rule
    // block keeps >=1 run so styleFor()/renderers never index an empty
    // vector, even when every character turned out to be whitespace.
    if (runs_.empty()) runs_.push_back({"", false, false, false});
    Block b;
    b.type = type_;
    b.srcOffset = offset_;
    b.listDepth = listDepth_;
    b.ordered = ordered_;
    b.runs = std::move(runs_);
    blocks_.push_back(std::move(b));
    runs_.clear();
    blockOpen_ = false;
    blockHasContent_ = false;
    spacePending_ = false;
    buf_.clear();
  }

  // Rule is the one BlockType with zero runs (InkDoc.h/MarkdownParser.h
  // convention) -- pushed directly rather than through flush()'s guard.
  void pushRule(uint32_t offset) {
    flush();
    Block b;
    b.type = BlockType::Rule;
    b.srcOffset = offset;
    blocks_.push_back(std::move(b));
  }

  // `inPre`: verbatim, no whitespace collapse. Inside <pre> the style is
  // forced to mono regardless of the nesting-depth flags -- InkDoc.h's
  // styleFor() already renders every Code-block run as mono, so folding
  // bold/italic/mono to (false,false,true) here keeps <pre> text merged
  // into a single run instead of splitting on every nested tag.
  void addChar(char c, uint32_t srcPos, bool inPre, bool bold, bool italic, bool mono) {
    if (inPre) {
      if (!blockOpen_) openBlock(BlockType::Code, srcPos);
      syncStyle(false, false, true);
      buf_ += c;
      blockHasContent_ = true;
      return;
    }
    if (isWsChar(c)) {
      // Collapse: only remembered if something is already on the page --
      // this is what trims leading whitespace for free (a block that has
      // seen no content yet just drops leading runs of whitespace).
      if (blockHasContent_) spacePending_ = true;
      return;
    }
    if (!blockOpen_) openBlock(BlockType::Body, srcPos);
    syncStyle(bold, italic, mono);
    if (spacePending_) {
      buf_ += ' ';
      spacePending_ = false;
    }
    buf_ += c;
    blockHasContent_ = true;
  }

 private:
  void syncStyle(bool bold, bool italic, bool mono) {
    if (bold != runBold_ || italic != runItalic_ || mono != runMono_) {
      flushBuf();
      runBold_ = bold;
      runItalic_ = italic;
      runMono_ = mono;
    }
  }

  void flushBuf() {
    if (!buf_.empty()) {
      runs_.push_back({buf_, runBold_, runItalic_, runMono_});
      buf_.clear();
    }
  }

  std::vector<Block> &blocks_;
  bool blockOpen_ = false;
  BlockType type_ = BlockType::Body;
  uint32_t offset_ = 0;
  uint8_t listDepth_ = 0;
  bool ordered_ = false;
  bool blockHasContent_ = false;
  bool spacePending_ = false;
  std::string buf_;
  bool runBold_ = false, runItalic_ = false, runMono_ = false;
  std::vector<Run> runs_;
};

}  // namespace

size_t decodeEntity(const std::string &s, size_t i, std::string &out) {
  size_t n = s.size();
  size_t limit = std::min(n, i + 1 + kEntityMaxName);
  size_t semi = std::string::npos;
  for (size_t j = i + 1; j < limit; ++j) {
    if (s[j] == ';') { semi = j; break; }
  }
  if (semi == std::string::npos) return 0;
  std::string name = s.substr(i + 1, semi - i - 1);
  if (name.empty()) return 0;
  size_t consumed = semi - i + 1;

  if (name == "amp") { out += '&'; return consumed; }
  if (name == "lt") { out += '<'; return consumed; }
  if (name == "gt") { out += '>'; return consumed; }
  if (name == "quot") { out += '"'; return consumed; }
  if (name == "apos") { out += '\''; return consumed; }
  if (name == "nbsp") { out += ' '; return consumed; }
  if (name == "mdash") { out += "--"; return consumed; }
  if (name == "ndash") { out += "-"; return consumed; }
  if (name == "hellip") { out += "..."; return consumed; }

  if (name[0] == '#') {
    if (name.size() < 2) return 0;
    bool hex = (name[1] == 'x' || name[1] == 'X');
    size_t k = hex ? 2 : 1;
    if (k >= name.size()) return 0;
    unsigned long cp = 0;
    for (; k < name.size(); ++k) {
      char c = name[k];
      int v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else return 0;
      cp = cp * (hex ? 16u : 10u) + (unsigned long)v;
    }
    appendUtf8(out, cp);
    return consumed;
  }
  return 0;  // unknown named entity -> caller emits it literally
}

std::string decodeEntities(const std::string &s) {
  std::string out;
  size_t n = s.size();
  size_t i = 0;
  while (i < n) {
    if (s[i] == '&') {
      size_t consumed = decodeEntity(s, i, out);
      if (consumed > 0) { i += consumed; continue; }
      out += '&';
      ++i;
      continue;
    }
    out += s[i++];
  }
  return out;
}

std::vector<Block> parseXhtml(const std::string &src) {
  std::vector<Block> blocks;
  Builder bd(blocks);
  size_t n = src.size();
  size_t i = findBodyStart(src);

  int boldDepth = 0, italicDepth = 0, monoDepth = 0;
  bool inPre = false;
  int quoteDepth = 0;
  std::vector<std::string> dropStack;    // nested script/style/head
  std::vector<ListEntry> listStack;      // nested ul/ol

  while (i < n) {
    char c = src[i];

    if (c == '<') {
      size_t tagStart = i;
      size_t limit = std::min(n, i + kTagWindow);
      size_t gt = findCharBounded(src, '>', i, limit);
      if (gt == std::string::npos) {
        // No '>' within the bounded window (including a genuinely missing
        // '>' before EOF, since findCharBounded's window is itself capped
        // at src.size()): tolerate by treating this one '<' as a literal
        // text character and re-scanning from the next byte.
        if (dropStack.empty())
          bd.addChar('<', (uint32_t)i, inPre, boldDepth > 0, italicDepth > 0, monoDepth > 0);
        ++i;
        continue;
      }

      size_t p = tagStart + 1;
      bool closing = false;
      if (p < gt && src[p] == '/') { closing = true; ++p; }
      size_t nameStart = p;
      while (p < gt && std::isalnum((unsigned char)src[p])) ++p;
      std::string tag = lowerStr(src.substr(nameStart, p - nameStart));
      bool selfClose = (gt > tagStart + 1 && src[gt - 1] == '/');
      i = gt + 1;

      bool isDropName = (tag == "script" || tag == "style" || tag == "head");

      if (!dropStack.empty()) {
        // While dropping, only script/style/head opens/closes matter --
        // everything else (including its text) is simply discarded.
        if (!closing && !selfClose && isDropName) dropStack.push_back(tag);
        else if (closing && isDropName) dropStack.pop_back();
        continue;
      }

      if (!closing && !selfClose && isDropName) {
        dropStack.push_back(tag);
        continue;
      }

      if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" ||
          tag == "h6") {
        if (!closing && !selfClose && quoteDepth == 0) {
          BlockType t = tag == "h1" ? BlockType::H1 : tag == "h2" ? BlockType::H2 : BlockType::H3;
          bd.openBlock(t, (uint32_t)tagStart);
        }
        continue;
      }

      if (tag == "p" || tag == "div") {
        if (!closing && !selfClose && quoteDepth == 0)
          bd.openBlock(BlockType::Body, (uint32_t)tagStart);
        continue;
      }

      if (tag == "blockquote") {
        // Whole subtree collapses into ONE Quote block: only the
        // outermost open/close transition touches block state.
        if (!closing && !selfClose) {
          if (quoteDepth == 0) bd.openBlock(BlockType::Quote, (uint32_t)tagStart);
          ++quoteDepth;
        } else if (closing && quoteDepth > 0) {
          --quoteDepth;
          if (quoteDepth == 0) bd.flush();
        }
        continue;
      }

      if (tag == "pre") {
        if (!closing && !selfClose && quoteDepth == 0) {
          bd.openBlock(BlockType::Code, (uint32_t)tagStart);
          inPre = true;
        } else if (closing && quoteDepth == 0 && inPre) {
          inPre = false;
          bd.flush();
        }
        continue;
      }

      if (tag == "ul" || tag == "ol") {
        if (!closing && !selfClose) listStack.push_back({tag == "ol"});
        else if (closing && !listStack.empty()) listStack.pop_back();
        continue;
      }

      if (tag == "li") {
        if (!closing && !selfClose && quoteDepth == 0) {
          uint8_t depth =
              listStack.empty() ? (uint8_t)1 : (uint8_t)std::min<size_t>(listStack.size(), 3);
          bool ordered = listStack.empty() ? false : listStack.back().ordered;
          bd.openBlock(BlockType::ListItem, (uint32_t)tagStart, depth, ordered);
        }
        continue;
      }

      if (tag == "hr") {
        if (!closing && quoteDepth == 0) bd.pushRule((uint32_t)tagStart);
        continue;
      }

      if (tag == "br") {
        if (!closing)
          bd.addChar(' ', (uint32_t)tagStart, inPre, boldDepth > 0, italicDepth > 0,
                     monoDepth > 0);
        continue;
      }

      if (tag == "em" || tag == "i") {
        if (!closing) ++italicDepth;
        else if (italicDepth > 0) --italicDepth;
        continue;
      }

      if (tag == "strong" || tag == "b") {
        if (!closing) ++boldDepth;
        else if (boldDepth > 0) --boldDepth;
        continue;
      }

      if (tag == "code" || tag == "tt") {
        if (!closing) ++monoDepth;
        else if (monoDepth > 0) --monoDepth;
        continue;
      }

      // Every other tag is transparent: skip its own markup, text inside
      // still flows through untouched.
      continue;
    }

    if (c == '&' && dropStack.empty()) {
      std::string decoded;
      size_t consumed = decodeEntity(src, i, decoded);
      if (consumed > 0) {
        for (char dc : decoded)
          bd.addChar(dc, (uint32_t)i, inPre, boldDepth > 0, italicDepth > 0, monoDepth > 0);
        i += consumed;
        continue;
      }
      bd.addChar('&', (uint32_t)i, inPre, boldDepth > 0, italicDepth > 0, monoDepth > 0);
      ++i;
      continue;
    }

    if (dropStack.empty())
      bd.addChar(c, (uint32_t)i, inPre, boldDepth > 0, italicDepth > 0, monoDepth > 0);
    ++i;
  }

  bd.flush();
  return blocks;
}

}  // namespace Ink
