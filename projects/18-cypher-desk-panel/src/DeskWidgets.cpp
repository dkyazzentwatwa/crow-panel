#include "DeskWidgets.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

// THE ONLY translation unit in this project that may include U8g2lib.h or name
// a u8g2 font.
//
// U8g2 declares its fonts as file-scope consts in the header, which in C++ is
// internal linkage: every .cpp that named u8g2_font_cubic11_h_cjk got a private
// 337,650-byte copy that the linker cannot merge with the others. Five copies
// were reaching the image - 1.69 MB of a 3 MB app partition. If you need
// compact type somewhere new, call DeskUi::smallText; do not include U8g2
// again.
#include <U8g2lib.h>

namespace DeskUi {

void smallText(Arduino_GFX *g, int16_t x, int16_t topY, const String &text, uint16_t color,
               Widgets::Align align) {
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

int16_t smallTextWidth(Arduino_GFX *g, const String &text) {
  g->setFont(u8g2_font_cubic11_h_cjk);
  g->setUTF8Print(true);
  g->setTextSize(1);
  int16_t bx, by;
  uint16_t bw, bh;
  g->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return static_cast<int16_t>(bw);
}

void button(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
            int16_t h, const String &label, bool active) {
  Widgets::panel(g, x + 3, y + 4, w, h, 10, theme.background);
  Widgets::panel(g, x, y, w, h, 10, active ? theme.panelHighlight : theme.panel, active ? 3 : 1,
                 active ? theme.accent2 : theme.line);
  smallText(g, x + w / 2, y + h / 2 - 3, label, active ? theme.ink : theme.muted,
            Widgets::kCenter);
}

void card(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
          int16_t h, uint16_t accent) {
  Widgets::panel(g, x + 4, y + 5, w, h, 13, theme.background);
  Widgets::panel(g, x, y, w, h, 13, theme.panel, 2, accent);
}

void progressBar(Arduino_GFX *g, const DeskThemePalette &theme, int16_t x, int16_t y, int16_t w,
                 int16_t h, uint16_t fraction, uint16_t accent) {
  if (fraction > 1000) fraction = 1000;
  g->fillRoundRect(x, y, w, h, h / 2, theme.panelHighlight);
  const int16_t filled = static_cast<int32_t>(w) * fraction / 1000;
  if (filled > h) g->fillRoundRect(x, y, filled, h, h / 2, accent);
  // Knob, so the bar reads as draggable rather than as a read-out.
  g->fillCircle(x + filled, y + h / 2, h, accent);
  g->drawCircle(x + filled, y + h / 2, h, theme.background);
}

}  // namespace DeskUi

#endif
