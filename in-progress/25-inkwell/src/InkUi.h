#ifndef INKWELL_INK_UI_H
#define INKWELL_INK_UI_H

#include "../config/ProjectConfig.h"
// Arduino.h before the target guard -- CONFIG_IDF_TARGET_ESP32P4 comes from
// sdkconfig.h via Arduino.h (see GfxMeasure.h's identical note).
#include <Arduino.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

#include "InkBook.h"
#include "LibraryStore.h"
#include "Paginator.h"

// The four touch screens beyond the reading page: library grid, HUD
// bottom sheet, contents list, Aa settings. One module rather than the
// plan's four files -- each view is a draw function plus a pure hit-test
// over the same geometry table, ~60 lines apiece; the pairing (draw and
// hit-test reading identical constants) is the property worth keeping
// adjacent, and it is documented as a deliberate deviation in the plan.
//
// Hit-tests are PURE (no state, no drawing): the .ino owns all state and
// calls the same action cores the serial commands use. NOT
// HARDWARE-VERIFIED.
namespace InkUi {

// ---- Library grid: 2x2 cards per page, portrait 600-wide geometry.
constexpr size_t kCardsPerPage = 4;

struct LibraryTap {
  enum Kind : uint8_t { None, Book, PagePrev, PageNext } kind = None;
  size_t index = 0;  // Book: absolute library index
};
void drawLibrary(Arduino_GFX *g, const LibraryStore &lib, size_t gridPage);
LibraryTap libraryHitTest(int16_t x, int16_t y, const LibraryStore &lib,
                          size_t gridPage);

// ---- HUD bottom sheet over the reading page.
struct HudTap {
  enum Kind : uint8_t {
    None, Outside, Scrub, Library, Contents, Aa, Flash, BriDown, BriUp
  } kind = None;
  uint16_t permille = 0;  // Scrub only
};
void drawHud(Arduino_GFX *g, uint16_t permille, uint8_t backlight,
             bool flashOn);
HudTap hudHitTest(int16_t x, int16_t y);
// The scrub bar's x -> permille map on its own, clamped to the bar: a live
// scrub DRAG tracks the finger's x even after it drifts off the bar's own
// y-band (hudHitTest would stop reporting Scrub there, which is right for
// taps but wrong for a grab already in progress).
uint16_t hudScrubPermilleAt(int16_t x);

// ---- Contents (TOC) list.
constexpr size_t kTocRowsPerPage = 14;

struct TocTap {
  enum Kind : uint8_t { None, Entry, Back, PagePrev, PageNext } kind = None;
  size_t index = 0;  // Entry: absolute TOC index
};
void drawToc(Arduino_GFX *g, const Ink::InkBook &book, size_t tocPage);
TocTap tocHitTest(int16_t x, int16_t y, const Ink::InkBook &book,
                  size_t tocPage);

// ---- Aa settings sheet.
struct AaTap {
  enum Kind : uint8_t { None, Font, Spacing, Margin, Flash, Back } kind = None;
  // Font: fontStep 0-2. Spacing: 100/115/130. Margin: 32/48/64.
  int16_t value = 0;
};
void drawAa(Arduino_GFX *g, const Ink::LayoutSettings &s, bool flashOn);
AaTap aaHitTest(int16_t x, int16_t y);

}  // namespace InkUi

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4

#endif
