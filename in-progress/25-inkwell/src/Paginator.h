#ifndef INKWELL_PAGINATOR_H
#define INKWELL_PAGINATOR_H

#include <cstdint>
#include <string>
#include <vector>
#include "InkDoc.h"

namespace Ink {

// Abstract text metrics so pagination is testable on host and identical on
// device (GfxMeasure wraps Arduino_GFX; tests use fixed per-style widths).
//
// Residual int16_t limit: textWidth() returns a pixel width in int16_t
// (max 32767). Paginator protects itself against the one case that
// actually occurs with real content -- a single unbroken word too long
// to fit any line -- by never trusting a measured width for a string
// longer than the current content width in CHARACTERS (a glyph is never
// narrower than 1px, so no fitting prefix can be longer than that many
// characters either); hard-split probes are then capped to that same
// length, so every textWidth() call this file makes is on a short,
// bounded prefix. It does NOT guard the fictional case of a short string
// paired with an implausibly wide per-glyph metric (e.g. a 40-char word
// at 1000px/char) -- that's assumed impossible for any real font on a
// panel this size, and is a caller (TextMeasure implementation)
// invariant, not one this class re-checks.
class TextMeasure {
 public:
  virtual ~TextMeasure() = default;
  virtual int16_t textWidth(const std::string &s, uint8_t style) = 0;
  virtual int16_t lineHeight(uint8_t style) = 0;
};

struct LayoutSettings {
  int16_t pageW = 600, pageH = 1024;
  int16_t marginX = 48, marginTop = 40, marginBottom = 64;  // bottom incl. footer
  uint8_t fontStep = 1;         // 0..2 — GfxMeasure/renderer map to font tables
  uint8_t lineSpacingPct = 115; // 100 / 115 / 130
  // Order + fields feed the sidecar-cache key; bump kLayoutVersion on change.
  uint32_t hash() const;
};
constexpr uint8_t kLayoutVersion = 1;

struct LineSeg { std::string text; uint8_t style; int16_t x; };
struct Line {
  std::vector<LineSeg> segs;
  int16_t height = 0;
  uint32_t srcOffset = 0;      // monotonic offset for resume (see spec)
  int16_t indentPx = 0;        // list bullets / quote bar drawn by renderer
  BlockType blockType = BlockType::Body;
  bool firstOfBlock = false;   // renderer draws bullet/quote-bar/rule here
};
struct Page { int firstLine = 0, lineCount = 0; };

// Lays out a whole chapter. Wall-clock is measure-bound: cache glyph advances
// inside the device TextMeasure, not here.
//
// Heap footprint: each Line costs roughly ~150 bytes (the segs vector's
// own overhead plus each LineSeg's std::string, usually short-string-
// optimized for wrapped chunks) plus about 3 small heap allocations
// (the segs vector's buffer, and any seg text past SSO). On ESP32 these
// are plain `new`/STL allocations that land in INTERNAL DRAM, not
// PSRAM -- there is no PSRAM allocator wired in here. A very long
// chapter's lines_ vector is therefore memory-bound on internal RAM
// well before it would trouble the 32MB PSRAM budget. Known risk,
// tracked for device bring-up rather than solved here.
class Paginator {
 public:
  void layout(const std::vector<Block> &blocks, const LayoutSettings &s,
              TextMeasure &m);
  const std::vector<Line> &lines() const { return lines_; }
  const std::vector<Page> &pages() const { return pages_; }
  size_t pageCount() const { return pages_.size(); }
  // Page whose [srcOffset of first line, next page's) range contains off.
  size_t pageForOffset(uint32_t off) const;
  uint32_t pageStartOffset(size_t page) const;

 private:
  std::vector<Line> lines_;
  std::vector<Page> pages_;
};

}  // namespace Ink

#endif
