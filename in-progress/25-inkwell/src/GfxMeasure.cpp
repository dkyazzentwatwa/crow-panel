// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT
// HARDWARE-VERIFIED -- metrics are read from the vendored font tables and
// are correct by construction, but nothing here has been observed on glass.
#include "GfxMeasure.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

// The vendored fonts are included from exactly ONE translation unit (this
// one) -- each header defines PROGMEM glyph/bitmap arrays, and const
// namespace-scope arrays have internal linkage, so a second including TU
// would duplicate kilobytes of flash per font for no benefit.
#include "fonts/FreeMono12pt7b.h"
#include "fonts/FreeMono18pt7b.h"
#include "fonts/FreeMono9pt7b.h"
#include "fonts/FreeSerif12pt7b.h"
#include "fonts/FreeSerif18pt7b.h"
#include "fonts/FreeSerif9pt7b.h"
#include "fonts/FreeSerifBold12pt7b.h"
#include "fonts/FreeSerifBold18pt7b.h"
#include "fonts/FreeSerifBold24pt7b.h"
#include "fonts/FreeSerifBold9pt7b.h"
#include "fonts/FreeSerifBoldItalic12pt7b.h"
#include "fonts/FreeSerifBoldItalic18pt7b.h"
#include "fonts/FreeSerifBoldItalic9pt7b.h"
#include "fonts/FreeSerifItalic12pt7b.h"
#include "fonts/FreeSerifItalic18pt7b.h"
#include "fonts/FreeSerifItalic9pt7b.h"

namespace InkwellGfx {

namespace {

// Font table decided at design time (spec 2026-08-06): body styles step
// through Serif 9/12/18pt; headings are bold one size up, clamped at 24pt;
// mono steps with the body. Fonts cover ASCII 0x20-0x7E only (stated in
// TECHNICAL.md; the paginator's byte-based hard-split shares the limit).
const GFXfont *kFontTable[Ink::kStyleCount][3] = {
    // step 0            step 1              step 2
    {&FreeSerif9pt7b, &FreeSerif12pt7b, &FreeSerif18pt7b},              // Body
    {&FreeSerifBold9pt7b, &FreeSerifBold12pt7b, &FreeSerifBold18pt7b},  // Bold
    {&FreeSerifItalic9pt7b, &FreeSerifItalic12pt7b,
     &FreeSerifItalic18pt7b},  // Italic
    {&FreeSerifBoldItalic9pt7b, &FreeSerifBoldItalic12pt7b,
     &FreeSerifBoldItalic18pt7b},                            // BoldItalic
    {&FreeMono9pt7b, &FreeMono12pt7b, &FreeMono18pt7b},      // Mono
    {&FreeSerifBold18pt7b, &FreeSerifBold24pt7b,
     &FreeSerifBold24pt7b},                                  // H1
    {&FreeSerifBold12pt7b, &FreeSerifBold18pt7b,
     &FreeSerifBold24pt7b},                                  // H2
    {&FreeSerifBold9pt7b, &FreeSerifBold12pt7b,
     &FreeSerifBold18pt7b},                                  // H3
};

// Lazy per-(style, fontStep) advance cache for ASCII 0x20-0x7E, filled
// straight from the GFXfont glyph xAdvance fields. Chars outside the
// font's range measure as the font's space advance (they draw as blanks).
struct AdvanceCache {
  bool filled = false;
  uint8_t adv[0x7F - 0x20];
};
AdvanceCache cache_[Ink::kStyleCount][3];

const AdvanceCache &cacheFor(uint8_t style, uint8_t step) {
  AdvanceCache &c = cache_[style][step];
  if (!c.filled) {
    const GFXfont *f = kFontTable[style][step];
    uint8_t spaceAdv = 6;
    if (f->first <= ' ' && ' ' <= f->last) {
      spaceAdv = f->glyph[' ' - f->first].xAdvance;
    }
    for (int ch = 0x20; ch < 0x7F; ++ch) {
      c.adv[ch - 0x20] = (f->first <= ch && ch <= f->last)
                             ? f->glyph[ch - f->first].xAdvance
                             : spaceAdv;
    }
    c.filled = true;
  }
  return c;
}

}  // namespace

const GFXfont *inkFont(uint8_t style, uint8_t fontStep) {
  if (style >= Ink::kStyleCount) style = Ink::kStyleBody;
  if (fontStep > 2) fontStep = 2;
  return kFontTable[style][fontStep];
}

int16_t GfxMeasure::textWidth(const std::string &s, uint8_t style) {
  if (style >= Ink::kStyleCount) style = Ink::kStyleBody;
  const AdvanceCache &c = cacheFor(style, fontStep_ > 2 ? 2 : fontStep_);
  // Accumulate in 32 bits and saturate: TextMeasure's contract is int16_t
  // (Task 6 review), and the Paginator's length guard depends on a wide
  // word measuring as "too wide", never as a wrapped-negative "fits".
  int32_t w = 0;
  for (char ch : s) {
    uint8_t u = (uint8_t)ch;
    w += (u >= 0x20 && u < 0x7F) ? c.adv[u - 0x20] : c.adv[0];
    if (w > 32767) return 32767;
  }
  return (int16_t)w;
}

int16_t GfxMeasure::lineHeight(uint8_t style) {
  return inkFont(style, fontStep_)->yAdvance;
}

}  // namespace InkwellGfx

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
