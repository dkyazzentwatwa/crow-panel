#ifndef CYPHER_DESK_WIDGETS_H
#define CYPHER_DESK_WIDGETS_H

#include "../config/ProjectConfig.h"
#include "DeskTypes.h"
#include <Arduino.h>

// The project-local drawing vocabulary, in one place.
//
// smallText existed in four near-identical copies (CypherDeskOs.cpp,
// DeskApp.cpp, DeskUtilityApplication.cpp, and a drawSmallCentered inside
// DeskTouchKeyboard.cpp), and there were two parallel button/card pairs
// (button/card vs osButton/osCard) that differed only in corner radius.
//
// That duplication was not just untidy, it was EXPENSIVE. U8g2 declares
// u8g2_font_cubic11_h_cjk as a file-scope const in its header, which gives it
// internal linkage - so every translation unit that named it got its own
// private 337,650-byte copy, and the linker cannot merge them. The binary
// carried FIVE copies: 1.69 MB, over half the 3 MB app partition, for one
// font. Everything below is DECLARED here and DEFINED in DeskWidgets.cpp
// precisely so exactly one translation unit ever names that font.
//
// These wrap shared/CrowPanelShared Widgets:: rather than replacing it. What
// lives here is the styling specific to this project - the compact type, the
// offset drop shadow, and the accent-bordered card.

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>

namespace DeskUi {

// Compact bitmap type. `topY` is the TOP of the glyph box, not the baseline,
// which is what makes these calls line up with the panel rectangles above them.
void smallText(Arduino_GFX *g, int16_t x, int16_t topY, const String &text, uint16_t color,
               Widgets::Align align = Widgets::kLeft);

int16_t smallTextWidth(Arduino_GFX *g, const String &text);

void button(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
            int16_t h, const String &label, bool active = false);

void card(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
          int16_t h, uint16_t accent);

// Horizontal progress / scrub bar. `fraction` is 0..1000 so callers need no
// floats.
void progressBar(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
                 int16_t h, uint16_t fraction, uint16_t accent);

// RGB565 linear blend, t in 0..255. Ported from project 21's splash - useful
// anywhere a fade is wanted and there is nothing else like it in the repo.
inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  const int16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int16_t r = ar + ((br - ar) * t) / 255;
  const int16_t gg = ag + ((bg - ag) * t) / 255;
  const int16_t bl = ab + ((bb - ab) * t) / 255;
  return static_cast<uint16_t>((r << 11) | (gg << 5) | bl);
}

}  // namespace DeskUi

#endif  // USE_DISPLAY

// Rect hit-test, needed in headless builds too (the host tests drive layout
// arithmetic without a display).
inline bool deskInside(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

#endif
