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
// (button/card vs osButton/osCard) that differed only in corner radius. Every
// new screen was copying one of them again.
//
// These wrap shared/CrowPanelShared Widgets:: rather than replacing it. What
// lives here is the small amount of styling that is specific to this project -
// the U8g2 compact type, the offset drop shadow, and the accent-bordered card.

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#include <U8g2lib.h>

namespace DeskUi {

// Compact bitmap type. `topY` is the TOP of the glyph box, not the baseline,
// which is what makes these calls line up with the panel rectangles above them.
inline void smallText(Arduino_GFX *g, int16_t x, int16_t topY, const String &text,
                      uint16_t color, Widgets::Align align = Widgets::kLeft) {
  g->setFont(u8g2_font_cubic11_h_cjk);
  g->setUTF8Print(true);
  g->setTextSize(1);
  g->setTextColor(color);
  int16_t bx, by;
  uint16_t bw, bh;
  g->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  int16_t drawX = x - bx;
  if (align == Widgets::kCenter) drawX = x - static_cast<int16_t>(bw) / 2 - bx;
  if (align == Widgets::kRight) drawX = x - static_cast<int16_t>(bw) - bx;
  g->setCursor(drawX, topY - by);
  g->print(text);
}

inline int16_t smallTextWidth(Arduino_GFX *g, const String &text) {
  g->setFont(u8g2_font_cubic11_h_cjk);
  g->setUTF8Print(true);
  g->setTextSize(1);
  int16_t bx, by;
  uint16_t bw, bh;
  g->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return static_cast<int16_t>(bw);
}

inline void button(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y,
                   int16_t w, int16_t h, const String &label, bool active = false) {
  Widgets::panel(g, x + 3, y + 4, w, h, 10, theme.background);
  Widgets::panel(g, x, y, w, h, 10, active ? theme.panelHighlight : theme.panel, active ? 3 : 1,
                 active ? theme.accent2 : theme.line);
  smallText(g, x + w / 2, y + h / 2 - 3, label, active ? theme.ink : theme.muted,
            Widgets::kCenter);
}

inline void card(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
                 int16_t h, uint16_t accent) {
  Widgets::panel(g, x + 4, y + 5, w, h, 13, theme.background);
  Widgets::panel(g, x, y, w, h, 13, theme.panel, 2, accent);
}

// Horizontal progress / scrub bar. `fraction` is 0..1000 so callers need no
// floats.
inline void progressBar(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y,
                        int16_t w, int16_t h, uint16_t fraction, uint16_t accent) {
  if (fraction > 1000) fraction = 1000;
  g->fillRoundRect(x, y, w, h, h / 2, theme.panelHighlight);
  const int16_t filled = static_cast<int32_t>(w) * fraction / 1000;
  if (filled > h) g->fillRoundRect(x, y, filled, h, h / 2, accent);
  // Knob, so the bar reads as draggable rather than as a read-out.
  g->fillCircle(x + filled, y + h / 2, h, accent);
  g->drawCircle(x + filled, y + h / 2, h, theme.background);
}

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
