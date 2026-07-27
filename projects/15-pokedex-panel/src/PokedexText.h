#ifndef POKEDEX_TEXT_H
#define POKEDEX_TEXT_H

#include "../config/ProjectConfig.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include <cstring>

namespace PokedexText {

enum Align {
  kLeft = 0,
  kCenter = 1,
  kRight = 2
};

// Keep the first hardware pass on the exact font Project 18 already proves on
// this panel. The broader U8g2 font catalog compiles cleanly but some faces do
// not render reliably through Arduino_GFX on this target.
inline const uint8_t *fontXL() { return u8g2_font_cubic11_h_cjk; }
inline const uint8_t *fontL() { return u8g2_font_cubic11_h_cjk; }
inline const uint8_t *fontM() { return u8g2_font_cubic11_h_cjk; }
inline const uint8_t *fontS() { return u8g2_font_cubic11_h_cjk; }

inline int16_t width(Arduino_GFX *g, const char *text, const uint8_t *font) {
  if (g == nullptr || text == nullptr) return 0;
  (void)font;
  // cubic11 is the Project 18-proven font. Avoid getTextBounds here: on the
  // P4 it repeatedly walks the 337 KB CJK table and can starve the first
  // display frame while wrapping long catalog notes.
  constexpr int16_t kApproxAdvance = 8;
  return static_cast<int16_t>(strlen(text) * kApproxAdvance);
}

inline void draw(Arduino_GFX *g, int16_t x, int16_t topY, const char *text,
                 const uint8_t *font, uint16_t color, Align align = kLeft) {
  if (g == nullptr || text == nullptr) return;
  int16_t textW = width(g, text, font);
  int16_t drawX = x;
  if (align == kCenter) drawX = x - textW / 2;
  if (align == kRight) drawX = x - textW;
  g->setFont(font);
  g->setUTF8Print(true);
  g->setTextSize(1);
  g->setTextColor(color);
  g->setCursor(drawX, topY + 10);
  g->print(text);
}

inline void draw(Arduino_GFX *g, int16_t x, int16_t topY, const String &text,
                 const uint8_t *font, uint16_t color, Align align = kLeft) {
  draw(g, x, topY, text.c_str(), font, color, align);
}

}  // namespace PokedexText

#endif

#endif
