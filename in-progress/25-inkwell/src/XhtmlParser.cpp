#include "XhtmlParser.h"

#include <algorithm>
#include <cctype>

namespace Ink {

namespace {

// Window bounds for the bounded scans below -- see MarkdownParser.cpp for
// why these must be bounded rather than an unbounded find(): a pathological
// unmatched '<' with no '>' anywhere later in the input would otherwise
// turn every character's lookahead into an O(n) scan, making the whole
// document O(n^2). The comment/PI/raw-text scans further down are each a
// single linear pass fired once per element (not once per character), so
// they stay O(n) in aggregate without needing this same bound.
// Named kTagCloseWindow (not kTagWindow) to avoid colliding in meaning
// with MarkdownParser.cpp's kTagWindow, a *different* 64-byte bound for
// its own inline "<...>"-stripping lookahead -- the two files' windows
// are unrelated and must not be assumed equal.
constexpr size_t kTagCloseWindow = 512;  // bytes scanned ahead of '<' for '>'
constexpr size_t kEntityMaxName = 12;    // max chars between '&' and ';'

bool isWsChar(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string lowerStr(const std::string &s) {
  std::string out(s.size(), '\0');
  for (size_t i = 0; i < s.size(); ++i) out[i] = (char)std::tolower((unsigned char)s[i]);
  return out;
}

// Scans forward from `from` for the tag's closing '>', treating anything
// inside a single- or double-quoted span as inert so a quoted attribute
// value containing '>' (e.g. title="a>b") can't end the tag early.
// Bounded to `limit` for the same O(n) reason as kTagCloseWindow above;
// an unterminated quote inside the window reports "no tag found" (npos),
// same as a plain missing '>'.
size_t findTagCloseBounded(const std::string &s, size_t from, size_t limit) {
  size_t end = std::min(s.size(), limit);
  char quote = 0;
  for (size_t j = from; j < end; ++j) {
    char c = s[j];
    if (quote) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '"' || c == '\'') { quote = c; continue; }
    if (c == '>') return j;
  }
  return std::string::npos;
}

// Single linear case-insensitive search for `needle` starting at `from`.
// Called once per <script>/<style> OPEN tag encountered -- never once per
// character -- so summed across the whole document this is still O(n)
// total, the same bound the rest of the file relies on: each byte of raw
// element content is visited by exactly one such scan.
size_t findCaseInsensitiveOnce(const std::string &s, const std::string &needle, size_t from) {
  size_t n = s.size(), m = needle.size();
  if (m == 0 || from >= n || m > n - from) return std::string::npos;
  for (size_t j = from; j + m <= n; ++j) {
    size_t k = 0;
    while (k < m &&
           std::tolower((unsigned char)s[j + k]) == std::tolower((unsigned char)needle[k]))
      ++k;
    if (k == m) return j;
  }
  return std::string::npos;
}

// Appends the UTF-8 encoding of code point `cp`. Code point 0 (e.g. a
// literal &#0;) emits nothing rather than a NUL byte; code points above
// 0xFFFF (outside the range InkDoc's rendering pipeline is sized for)
// emit '?' per the documented degrade. `cp` is a fixed-width uint32_t
// (never `unsigned long`, which is 64-bit on the host but 32-bit on the
// ESP32 target) so overflow behavior for a huge numeric entity can't
// silently differ between the two -- see the saturation in decodeEntity.
void appendUtf8(std::string &out, uint32_t cp) {
  if (cp == 0) return;
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
// forward pass over '<' occurrences; the extra tag-close scan fires at
// most once (only once a body tag is actually spotted), and each comment
// is skipped in one linear scan too, so this stays O(n) overall rather
// than O(n^2).
size_t findBodyStart(const std::string &src) {
  size_t n = src.size();
  size_t i = 0;
  while (i < n) {
    size_t lt = src.find('<', i);
    if (lt == std::string::npos) return 0;
    // Skip comments entirely -- a "<body>" mentioned inside one (e.g. an
    // authoring note "<!-- old <body> -->") must not be mistaken for the
    // real tag.
    if (src.compare(lt, 4, "<!--") == 0) {
      size_t close = src.find("-->", lt + 4);
      i = (close == std::string::npos) ? n : close + 3;
      continue;
    }
    size_t j = lt + 1;
    if (j < n && src[j] == '/') { i = j; continue; }
    size_t nameStart = j;
    while (j < n && std::isalnum((unsigned char)src[j])) ++j;
    if (lowerStr(src.substr(nameStart, j - nameStart)) == "body") {
      // Quote-aware, like the main tokenizer's tag scan -- a
      // <body class="a>b"> attribute value containing '>' must not end
      // the tag early.
      size_t limit = std::min(n, lt + kTagCloseWindow);
      size_t gt = findTagCloseBounded(src, lt, limit);
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

  // Flushes whatever is pending. A block that never accumulated any real
  // text (e.g. a <div> immediately superseded by a nested block-opening
  // tag before any text arrived) is a phantom, not content, and is
  // discarded rather than materialized with a placeholder empty run --
  // Rule is pushed separately via pushRule() and is unaffected.
  //
  // blockHasContent_ is provably identical to "runs_ contains a run with
  // non-empty text" at this point: it's set true the moment (and only
  // the moment) any real character is appended, in the same addChar()
  // call that appends it to buf_/runs_, and never cleared except when a
  // block opens or flushes -- so it's a free O(1) stand-in for rescanning
  // runs_ for non-empty text.
  void flush() {
    if (!blockOpen_) return;
    flushBuf();
    if (blockHasContent_) {
      Block b;
      b.type = type_;
      b.srcOffset = offset_;
      b.listDepth = listDepth_;
      b.ordered = ordered_;
      b.runs = std::move(runs_);
      blocks_.push_back(std::move(b));
    }
    runs_.clear();
    blockOpen_ = false;
    blockHasContent_ = false;
    spacePending_ = false;
    buf_.clear();
  }

  // Rule is the one BlockType with zero runs (InkDoc.h/MarkdownParser.h
  // convention) -- pushed directly rather than through flush()'s text check.
  void pushRule(uint32_t offset) {
    flush();
    Block b;
    b.type = BlockType::Rule;
    b.srcOffset = offset;
    blocks_.push_back(std::move(b));
  }

  // Used for a block-boundary tag (p/div/h*/li, open or close) seen while
  // it is being merged transparently into an enclosing block instead of
  // starting a new one (a blockquote's minified "<p>a</p><p>b</p>") --
  // inserts a single collapsible space between the joined segments, the
  // same as a real whitespace run between them would have.
  void markSoftBreak() {
    if (blockHasContent_) spacePending_ = true;
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
    // Fixed-width uint32_t, saturated every step once past the valid
    // Unicode range: kEntityMaxName allows up to ~10 decimal or ~9 hex
    // digits, comfortably enough to overflow a 32-bit accumulator (e.g.
    // "&#4294967296;" or "&#x100000000;", both exactly 2^32). Without
    // saturating, a 32-bit `unsigned long` (the ESP32 target) would wrap
    // to a small, plausible-looking code point while a 64-bit host
    // wouldn't -- a silent firmware/host behavioral split no test run on
    // the host alone could ever catch. Saturating well above 0x10FFFF as
    // soon as it's exceeded means every further digit is a no-op, so the
    // final value (and thus appendUtf8's ">0xFFFF -> '?'" rule) is
    // identical on both widths.
    uint32_t cp = 0;
    for (; k < name.size(); ++k) {
      char c = name[k];
      int v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else return 0;
      cp = cp * (hex ? 16u : 10u) + (uint32_t)v;
      if (cp > 0x110000u) cp = 0x110000u;
    }
    appendUtf8(out, cp);  // cp == 0 emits nothing; see appendUtf8.
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
  int headDepth = 0;                 // nested <head>...</head> depth
  std::vector<ListEntry> listStack;  // nested ul/ol

  while (i < n) {
    char c = src[i];

    if (c == '<') {
      char next = (i + 1 < n) ? src[i + 1] : '\0';
      bool looksLikeTag =
          std::isalpha((unsigned char)next) || next == '/' || next == '!' || next == '?';
      if (!looksLikeTag) {
        // HTML5 rule: '<' only opens a tag when followed by a letter,
        // '/', '!' or '?' -- anything else (space, digit, EOF, ...) is
        // literal text. Advance one byte so a later real '<' elsewhere
        // is still found normally.
        if (headDepth == 0)
          bd.addChar('<', (uint32_t)i, inPre, boldDepth > 0, italicDepth > 0, monoDepth > 0);
        ++i;
        continue;
      }

      if (next == '!') {
        // Comment or bogus declaration (DOCTYPE etc.) -- one linear scan
        // fired once for this element, not once per character, so this
        // stays O(n) in aggregate across the whole document.
        if (src.compare(i, 4, "<!--") == 0) {
          size_t close = src.find("-->", i + 4);
          i = (close == std::string::npos) ? n : close + 3;
        } else {
          size_t gtb = src.find('>', i + 2);
          i = (gtb == std::string::npos) ? n : gtb + 1;
        }
        continue;
      }
      if (next == '?') {
        // Processing instruction (e.g. <?xml version="1.0"?>) -- skip to
        // the next '>', one linear scan for this element.
        size_t gtb = src.find('>', i + 2);
        i = (gtb == std::string::npos) ? n : gtb + 1;
        continue;
      }

      size_t tagStart = i;
      size_t limit = std::min(n, i + kTagCloseWindow);
      size_t gt = findTagCloseBounded(src, i, limit);
      if (gt == std::string::npos) {
        // No unquoted '>' within the bounded window (an unterminated
        // quote inside the window counts as this too): tolerate as a
        // literal '<'.
        if (headDepth == 0)
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

      if (tag == "script" || tag == "style") {
        // Raw-text element: its content is opaque and must never be
        // re-tokenized as markup (a stray '<' from e.g. "if(a<b)" or a
        // CSS selector must not be mistaken for a tag). One linear scan
        // for the literal closer, then one more for its own '>' -- both
        // run once per <script>/<style> element, not once per character,
        // so the whole document stays O(n) even with many such elements.
        if (!closing && !selfClose) {
          std::string closer = "</" + tag;
          size_t found = findCaseInsensitiveOnce(src, closer, i);
          if (found == std::string::npos) { i = n; continue; }
          size_t gt2 = src.find('>', found);
          i = (gt2 == std::string::npos) ? n : gt2 + 1;
        }
        // A stray closing tag (no matching open we handled) or a
        // self-closed <script/> has no content to discard.
        continue;
      }

      if (tag == "head") {
        if (!closing && !selfClose) ++headDepth;
        else if (closing && headDepth > 0) --headDepth;
        continue;
      }

      if (headDepth > 0) continue;  // discard everything else inside <head>

      if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" ||
          tag == "h6") {
        if (quoteDepth > 0) {
          bd.markSoftBreak();
        } else if (!closing && !selfClose) {
          BlockType t = tag == "h1" ? BlockType::H1 : tag == "h2" ? BlockType::H2 : BlockType::H3;
          bd.openBlock(t, (uint32_t)tagStart);
        } else if (closing) {
          // Ends the heading at top level: text right after (even with no
          // intervening whitespace, e.g. "<h1>Title</h1>stray") must
          // start a fresh Body block, not glue onto the heading's text
          // and style.
          bd.flush();
        }
        continue;
      }

      if (tag == "p" || tag == "div") {
        if (quoteDepth > 0) {
          bd.markSoftBreak();
        } else if (!closing && !selfClose) {
          bd.openBlock(BlockType::Body, (uint32_t)tagStart);
        } else if (closing && tag == "p") {
          bd.flush();
        } else if (closing && tag == "div") {
          // Unlike p/h*/li, </div> does NOT end the block -- div is a
          // soft grouping wrapper, not a hard paragraph break, so
          // adjacent <div>s merge into one block with a joining space
          // instead of splitting (testXhtmlNestedDivMerges).
          bd.markSoftBreak();
        }
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
        if (quoteDepth > 0) {
          bd.markSoftBreak();
        } else if (!closing && !selfClose) {
          uint8_t depth =
              listStack.empty() ? (uint8_t)1 : (uint8_t)std::min<size_t>(listStack.size(), 3);
          bool ordered = listStack.empty() ? false : listStack.back().ordered;
          bd.openBlock(BlockType::ListItem, (uint32_t)tagStart, depth, ordered);
        } else if (closing) {
          bd.flush();
        }
        continue;
      }

      if (tag == "td" || tag == "th" || tag == "tr" || tag == "caption") {
        // No grid model: a table's rows/cells just flatten into one
        // run-on paragraph, with a joining space at each cell/row/caption
        // boundary (open or close, in or out of a quote) -- the same
        // treatment <br> gets.
        bd.markSoftBreak();
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

    if (c == '&' && headDepth == 0) {
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

    if (headDepth == 0)
      bd.addChar(c, (uint32_t)i, inPre, boldDepth > 0, italicDepth > 0, monoDepth > 0);
    ++i;
  }

  bd.flush();
  return blocks;
}

}  // namespace Ink
