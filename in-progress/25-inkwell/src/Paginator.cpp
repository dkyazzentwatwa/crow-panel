#include "Paginator.h"

#include <algorithm>

// Measure-call complexity for the whole file: textWidth() is called up to
// TWICE per normal (non-hard-split) word -- once for the word itself, and
// once more for the joining space if one precedes it -- never per
// character in a loop that re-measures a growing prefix. A word that
// needs hard-splitting instead costs one measurement per binary-search
// probe PER CHUNK: O(log(min(remaining chars, contentW))) probes, each on
// a prefix bounded to at most contentW characters (see hardSplitLen) so
// the measured pixel width can't overflow int16_t. On device, textWidth
// is the bottleneck (glyph metrics), not this file's control flow.

namespace Ink {

namespace {

// Fixed layout constants named so the 24s scattered through this file
// are visibly the same handful of design choices, not four coincidental
// numbers.
constexpr int16_t kListIndentPerDepthPx = 24;  // ListItem: 24 * listDepth
constexpr int16_t kQuoteIndentPx = 24;         // Quote: flat, not depth-scaled
constexpr int16_t kRuleHeightPx = 24;          // Rule: fixed, never measured
constexpr int kParagraphGapPct = 40;           // % of raw body line height

// Indent reserved for the block's own bullet/quote-bar (drawn by the
// renderer, not here) -- ListItem scales with nesting depth; Quote is a
// flat bar regardless of depth (Block has no quote-nesting field).
int16_t indentForBlock(const Block &b) {
  if (b.type == BlockType::ListItem) {
    uint8_t depth = b.listDepth ? b.listDepth : 1;
    return (int16_t)(kListIndentPerDepthPx * depth);
  }
  if (b.type == BlockType::Quote) return kQuoteIndentPx;
  return 0;
}

int16_t scaledHeight(int16_t raw, uint8_t lineSpacingPct) {
  return (int16_t)((int32_t)raw * lineSpacingPct / 100);
}

struct WordTok {
  std::string text;
  uint8_t style;
  bool spaceBefore;  // false at block start, or when glued to the
                      // previous word by a style change with no space
                      // between (e.g. inline emphasis mid-word).
  uint32_t startPos;  // char offset within the block's concatenated
                       // plain text (runs' text, in order).
};

// Tokenizes one non-Code block into words: a single pass over every run's
// characters (O(total block text length), no re-scans). A "word" is a
// maximal span of non-space characters that share one style; a style
// change with no intervening space still ends the current word (so
// "bo**ld**" renders as two adjacent, unmerged segs with no visible gap
// rather than one mixed-style seg or an incorrect line break).
//
// Splitting only on literal ' ' relies on a cross-module invariant: every
// emitting parser (TxtParser / MarkdownParser / XhtmlParser) already
// normalizes whitespace -- tabs, embedded newlines, runs of spaces -- down
// to single ' ' characters per the shared InkDoc contract. Paginator does
// not re-normalize; a raw '\t' or '\n' reaching here from a non-Code block
// would be treated as an ordinary (if unusual) word character.
void tokenizeWords(const Block &b, std::vector<WordTok> &words) {
  uint32_t pos = 0;
  bool active = false;
  std::string cur;
  uint8_t curStyle = 0;
  uint32_t curStart = 0;
  bool wordSpaceBefore = false;
  bool sawSpace = false;

  auto flushWord = [&]() {
    if (!active) return;
    words.push_back({cur, curStyle, wordSpaceBefore, curStart});
    active = false;
    cur.clear();
  };

  for (const Run &r : b.runs) {
    uint8_t style = styleFor(b, r);
    for (char c : r.text) {
      if (c == ' ') {
        flushWord();
        sawSpace = true;
        ++pos;
        continue;
      }
      if (active && curStyle == style) {
        cur.push_back(c);
        ++pos;
        continue;
      }
      flushWord();
      active = true;
      curStyle = style;
      curStart = pos;
      wordSpaceBefore = sawSpace;
      sawSpace = false;
      cur.push_back(c);
      ++pos;
    }
  }
  flushWord();
}

// Largest prefix length (>=1) of word[start..] whose measured width fits
// within contentW. Binary search bounded to at most contentW characters
// -- a glyph is never narrower than 1px, so no fitting prefix can ever be
// longer than that many characters -- which does two things at once:
// keeps every measured substring short enough that its pixel width can't
// overflow int16_t, and lets the caller walk an arbitrarily long
// unbreakable word by index instead of copying/erasing a shrinking tail
// (that copy-per-chunk pattern is what made the naive version O(n^2) in
// the word's length). O(log(min(remaining, contentW))) textWidth() calls.
// Always returns at least 1: guarantees forward progress even when
// contentW is smaller than one character.
size_t hardSplitLen(const std::string &word, size_t start, uint8_t style,
                     int16_t contentW, TextMeasure &m) {
  size_t avail = word.size() - start;
  if (avail == 0) return 0;
  size_t cap = std::min(avail, (size_t)contentW);
  if (m.textWidth(word.substr(start, 1), style) > contentW) return 1;
  size_t lo = 1, hi = cap, best = 1;
  while (lo <= hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (m.textWidth(word.substr(start, mid), style) <= contentW) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

// Greedy word-wrap of one block's word stream into lines, calling
// emitLine(segs, relOffset) for every completed line -- normal wraps and
// each hard-split chunk (including the final remnant) alike. relOffset is
// the char offset (within the block's plain text) of the line's first
// consumed character, monotonic across calls.
template <typename EmitFn>
void wrapWords(const std::vector<WordTok> &words, int16_t contentW,
                TextMeasure &m, EmitFn emitLine) {
  std::vector<LineSeg> segs;
  int16_t lineWidth = 0;
  uint32_t lineStartPos = 0;
  bool lineHasContent = false;

  auto flushLine = [&]() {
    emitLine(std::move(segs), lineStartPos);
    segs.clear();  // defensive: std::vector doesn't guarantee empty-after-
                    // move, only valid-but-unspecified; we reuse segs next.
    lineWidth = 0;
    lineHasContent = false;
  };

  for (const WordTok &w : words) {
    int16_t wordW = m.textWidth(w.text, w.style);

    // A glyph is never narrower than 1px, so a word longer than contentW
    // characters can never fit a line regardless of what textWidth()
    // reports -- and it's exactly the case where a long enough word times
    // a wide enough font would have overflowed textWidth()'s int16_t
    // return (wrapping to something small or negative and slipping past
    // the "wordW > contentW" check below). Checking the length up front
    // catches that unconditionally, before the (possibly garbage)
    // measured width is trusted at all.
    bool tooWide = wordW > contentW || w.text.size() > (size_t)contentW;

    if (tooWide) {
      // Unbreakable word wider than a full line: finish whatever line is
      // pending, then hard-split by characters. Every chunk -- including
      // the final remnant -- ends its own line; the next word (if any)
      // always starts a fresh line rather than sharing one with a
      // hard-split tail.
      if (lineHasContent) flushLine();
      size_t n = w.text.size();
      size_t pos = 0;
      uint32_t chunkPos = w.startPos;
      while (pos < n) {
        size_t k = hardSplitLen(w.text, pos, w.style, contentW, m);
        std::vector<LineSeg> chunkSegs;
        chunkSegs.push_back({w.text.substr(pos, k), w.style, 0});
        emitLine(std::move(chunkSegs), chunkPos);
        pos += k;
        chunkPos += (uint32_t)k;
      }
      continue;
    }

    int16_t spaceW = (w.spaceBefore && lineHasContent)
                          ? m.textWidth(" ", w.style)
                          : 0;
    if (lineHasContent && lineWidth + spaceW + wordW > contentW) {
      flushLine();
      spaceW = 0;  // fresh line: no leading space is ever rendered
    }

    if (!lineHasContent) {
      segs.push_back({w.text, w.style, 0});
      lineWidth = wordW;
      lineStartPos = w.startPos;
      lineHasContent = true;
    } else if (!segs.empty() && segs.back().style == w.style) {
      // Same-style neighbor: tokenizeWords already fused any style-glued
      // run, so two adjacent words of the same style always had a space
      // between them in the source -- merge into one seg with that space.
      segs.back().text += ' ';
      segs.back().text += w.text;
      lineWidth += spaceW + wordW;
    } else {
      int16_t x = (int16_t)(lineWidth + spaceW);
      segs.push_back({w.text, w.style, x});
      lineWidth = (int16_t)(x + wordW);
    }
  }
  if (lineHasContent) flushLine();
}

// Code blocks are never rewrapped: every source '\n'-delimited line
// becomes exactly one Line, verbatim, regardless of measured width.
// Concatenates all runs' text first so a code block split across runs
// still produces one coherent stream of source lines. Always emits at
// least one line (even an entirely empty code block gets one blank
// line), so callers never need a separate empty-block fallback for Code.
//
// A code block whose text ends in '\n' produces one extra, empty trailing
// Line (splitting "a\n" on '\n' yields ["a", ""], same as any standard
// line-split). Left as-is rather than special-cased away: a source file
// that legitimately ends in a blank line should still render one.
template <typename EmitFn>
void emitCodeLines(const Block &b, EmitFn emitLine) {
  std::string cur;
  uint32_t pos = 0;
  uint32_t lineStart = 0;
  for (const Run &r : b.runs) {
    for (char c : r.text) {
      if (c == '\n') {
        std::vector<LineSeg> segs;
        segs.push_back({cur, kStyleMono, 0});
        emitLine(std::move(segs), lineStart);
        cur.clear();
        ++pos;
        lineStart = pos;
        continue;
      }
      cur.push_back(c);
      ++pos;
    }
  }
  std::vector<LineSeg> segs;
  segs.push_back({cur, kStyleMono, 0});
  emitLine(std::move(segs), lineStart);
}

}  // namespace

uint32_t LayoutSettings::hash() const {
  // FNV-1a 32-bit, field by field (not a struct memcpy -- padding between
  // int16_t/uint8_t members would make that non-portable and would pull
  // uninitialized bytes into the hash).
  uint32_t h = 2166136261u;
  auto mixByte = [&](uint8_t byte) {
    h ^= byte;
    h *= 16777619u;
  };
  auto mixBytes = [&](uint32_t v, int nbytes) {
    for (int i = 0; i < nbytes; ++i) mixByte((uint8_t)((v >> (8 * i)) & 0xFF));
  };
  mixByte(kLayoutVersion);
  mixBytes((uint16_t)pageW, 2);
  mixBytes((uint16_t)pageH, 2);
  mixBytes((uint16_t)marginX, 2);
  mixBytes((uint16_t)marginTop, 2);
  mixBytes((uint16_t)marginBottom, 2);
  mixByte(fontStep);
  mixByte(lineSpacingPct);
  return h;
}

void Paginator::layout(const std::vector<Block> &blocks,
                        const LayoutSettings &s, TextMeasure &m) {
  lines_.clear();
  pages_.clear();

  for (const Block &b : blocks) {
    int16_t indentPx = indentForBlock(b);
    int16_t contentW = (int16_t)(s.pageW - 2 * s.marginX - indentPx);
    if (contentW < 1) contentW = 1;  // guard: never let width collapse to <=0

    size_t blockFirstLine = lines_.size();
    bool firstOfBlock = true;

    auto emit = [&](std::vector<LineSeg> segs, uint32_t relOffset) {
      Line ln;
      ln.segs = std::move(segs);
      int16_t rawH;
      if (ln.segs.empty()) {
        rawH = m.lineHeight(kStyleBody);  // blank-paragraph placeholder
      } else {
        rawH = 0;
        for (const LineSeg &sg : ln.segs)
          rawH = std::max(rawH, m.lineHeight(sg.style));
      }
      ln.height = scaledHeight(rawH, s.lineSpacingPct);
      // relOffset counts plain-text characters consumed within the block;
      // adding it to the block's byte-offset base stays monotonic and
      // in-bounds because a block's plain text is always <= its source
      // span (markup/entities collapse to fewer chars, never more) -- so
      // this can undershoot a byte position but never run past the next
      // block's start.
      ln.srcOffset = b.srcOffset + relOffset;
      ln.indentPx = indentPx;
      ln.blockType = b.type;
      ln.firstOfBlock = firstOfBlock;
      firstOfBlock = false;
      lines_.push_back(std::move(ln));
    };

    if (b.type == BlockType::Rule) {
      Line ln;
      ln.height = kRuleHeightPx;  // fixed, never measured
      ln.srcOffset = b.srcOffset;
      ln.indentPx = 0;
      ln.blockType = BlockType::Rule;
      ln.firstOfBlock = true;
      lines_.push_back(std::move(ln));
    } else if (b.type == BlockType::Code) {
      emitCodeLines(b, emit);
    } else {
      std::vector<WordTok> words;
      tokenizeWords(b, words);
      if (words.empty()) {
        emit({}, 0);  // blank paragraph: still takes a line of vertical space
      } else {
        wrapWords(words, contentW, m, emit);
      }
    }

    // Paragraph gap: a flat 40% of the RAW (unscaled) body line height,
    // added to the block's last line -- simpler than synthesizing an
    // extra blank line, and deliberately independent of lineSpacingPct.
    // Applies uniformly to every block type, Rule included (the spec
    // carves out no exception for it).
    if (lines_.size() > blockFirstLine) {
      int16_t gap =
          (int16_t)((int32_t)m.lineHeight(kStyleBody) * kParagraphGapPct / 100);
      lines_.back().height = (int16_t)(lines_.back().height + gap);
    }
  }

  // Page fill.
  int16_t capacity = (int16_t)(s.pageH - s.marginTop - s.marginBottom);
  if (capacity < 1) capacity = 1;  // guard against pathological margins

  if (lines_.empty()) {
    pages_.push_back({0, 0});  // empty chapter = one blank page
    return;
  }

  size_t i = 0;
  while (i < lines_.size()) {
    size_t start = i;
    int32_t used = 0;
    int count = 0;
    while (i < lines_.size()) {
      int16_t h = lines_[i].height;
      if (used + h > capacity) break;
      bool isHeading = lines_[i].blockType == BlockType::H1 ||
                        lines_[i].blockType == BlockType::H2 ||
                        lines_[i].blockType == BlockType::H3;
      // Heading-orphan rule: don't let a heading become the LAST line
      // that fits on a page when more lines follow -- push it to the
      // next page instead. Exempt the case where the heading is already
      // the first line under consideration for this page (count == 0):
      // a fresh page has full capacity, so deferring there would either
      // not help (same problem repeats) or isn't a real orphan (nothing
      // precedes it to orphan it from).
      if (isHeading && count > 0 && i + 1 < lines_.size()) {
        int32_t remainingAfter = capacity - (used + h);
        if (remainingAfter < lines_[i + 1].height) break;
      }
      used += h;
      ++count;
      ++i;
    }
    if (count == 0) {
      // Forced progress: a single line taller than the page (or a
      // heading alone on an otherwise-empty page) still has to go
      // somewhere -- never stall.
      count = 1;
      ++i;
    }
    pages_.push_back({(int)start, count});
  }
}

uint32_t Paginator::pageStartOffset(size_t page) const {
  if (pages_.empty() || lines_.empty()) return 0;
  if (page >= pages_.size()) page = pages_.size() - 1;
  const Page &pg = pages_[page];
  if (pg.lineCount <= 0) return 0;
  return lines_[(size_t)pg.firstLine].srcOffset;
}

// Linear scan over pages (a chapter has at most a few hundred, so this is
// cheap and simple -- not worth a binary search over a vector that small;
// document the YAGNI rather than add index machinery). Early-breaks once
// a page's start offset passes `off`, relying on the same monotonic-
// srcOffset invariant documented at the emit() call site above (pages are
// built from lines_ in source order, so pageStartOffset() is
// non-decreasing in page index).
size_t Paginator::pageForOffset(uint32_t off) const {
  if (pages_.empty()) return 0;
  size_t result = 0;
  for (size_t p = 0; p < pages_.size(); ++p) {
    uint32_t start = pageStartOffset(p);
    if (start > off) break;
    result = p;
  }
  return result;
}

}  // namespace Ink
