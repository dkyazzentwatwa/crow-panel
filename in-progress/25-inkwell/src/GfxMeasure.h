#ifndef INKWELL_GFX_MEASURE_H
#define INKWELL_GFX_MEASURE_H

#include "../config/ProjectConfig.h"
// Arduino.h must precede the target guard: CONFIG_IDF_TARGET_ESP32P4
// comes from sdkconfig.h via Arduino.h, so testing it before this
// include silently compiles the whole file out (same reason
// DisplayBringup.h includes <Arduino.h> first).
#include <Arduino.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

#include "InkDoc.h"
#include "Paginator.h"

// Real-font metrics for the Paginator, replacing the fixed-table
// SerialMeasure when the display is up. Widths come straight from the
// vendored GFXfont glyph tables (xAdvance per glyph) -- no draw calls, no
// getTextBounds; a per-(style, fontStep) advance cache makes textWidth a
// table walk, keeping whole-chapter pagination in the tens of
// milliseconds on the P4.
namespace InkwellGfx {

// The (style, fontStep) -> vendored GFXfont mapping. fontStep 0-2; style is
// Ink::Style. Never returns null. Also used by ReaderView to set fonts for
// drawing, so measure and render can never disagree about metrics.
const GFXfont *inkFont(uint8_t style, uint8_t fontStep);

class GfxMeasure : public Ink::TextMeasure {
 public:
  explicit GfxMeasure(uint8_t fontStep) : fontStep_(fontStep) {}
  void setFontStep(uint8_t step) { fontStep_ = step; }

  int16_t textWidth(const std::string &s, uint8_t style) override;
  int16_t lineHeight(uint8_t style) override;

 private:
  uint8_t fontStep_ = 1;
};

}  // namespace InkwellGfx

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4

#endif
