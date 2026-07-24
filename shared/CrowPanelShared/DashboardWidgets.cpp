#include "DashboardWidgets.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

// Vendored FreeSans fonts (see fonts/README.md). Included in this single
// translation unit only, so the const GFXfont definitions raise no ODR issue.
#include "fonts/FreeSansBold24pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "fonts/FreeSans12pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"

namespace Widgets {

const GFXfont *fontXL() { return &FreeSansBold24pt7b; }
const GFXfont *fontL() { return &FreeSansBold12pt7b; }
const GFXfont *fontM() { return &FreeSans12pt7b; }
const GFXfont *fontS() { return &FreeSansBold9pt7b; }

namespace {
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
}  // namespace

int16_t textWidth(Arduino_GFX *g, const char *s, const GFXfont *font) {
  int16_t bx, by;
  uint16_t bw, bh;
  g->setFont(font);
  g->setTextSize(1);
  g->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  return (int16_t)bw;
}

void text(Arduino_GFX *g, int16_t x, int16_t y, const char *s,
          const GFXfont *font, uint16_t color, Align align) {
  int16_t bx, by;
  uint16_t bw, bh;
  g->setFont(font);
  g->setTextSize(1);
  g->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);

  int16_t drawX;
  if (align == kCenter) {
    drawX = x - (int16_t)(bw / 2) - bx;
  } else if (align == kRight) {
    drawX = x - (int16_t)bw - bx;
  } else {
    drawX = x - bx;
  }
  int16_t baselineY = y - by;  // by is the (negative) top offset from baseline

  g->setTextColor(color);
  g->setCursor(drawX, baselineY);
  g->print(s);
}

void panel(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
           int16_t radius, uint16_t fill, int16_t border, uint16_t borderColor) {
  g->fillRoundRect(x, y, w, h, radius, fill);
  for (int16_t i = 0; i < border; i++) {
    g->drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, radius, borderColor);
  }
}

void arcGauge(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t rOuter, int16_t rInner,
              float value01, uint16_t valueColor, uint16_t trackColor) {
  // 270-degree sweep with the gap at the bottom. Screen angles: 0 = 3 o'clock,
  // increasing clockwise. Start lower-left (135), sweep up over the top.
  const float start = 135.0f;
  const float sweep = 270.0f;
  value01 = clamp01(value01);
  g->fillArc(cx, cy, rOuter, rInner, start, start + sweep, trackColor);
  if (value01 > 0.001f) {
    g->fillArc(cx, cy, rOuter, rInner, start, start + sweep * value01, valueColor);
  }
}

void hBar(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
          float value01, uint16_t fill, uint16_t track) {
  int16_t r = h / 2;
  g->fillRoundRect(x, y, w, h, r, track);
  value01 = clamp01(value01);
  int16_t fw = (int16_t)(w * value01);
  if (fw < h) fw = (value01 > 0.001f) ? h : 0;  // keep the rounded cap legible
  if (fw > 0) {
    g->fillRoundRect(x, y, fw, h, r, fill);
  }
}

void signalBars(Arduino_GFX *g, int16_t x, int16_t baselineY, uint8_t level,
                uint16_t on, uint16_t off) {
  const int16_t bw = 7;
  const int16_t gap = 5;
  const int16_t heights[4] = {8, 14, 20, 26};
  for (int i = 0; i < 4; i++) {
    int16_t bx = x + i * (bw + gap);
    int16_t bh = heights[i];
    g->fillRoundRect(bx, baselineY - bh, bw, bh, 2, (i < level) ? on : off);
  }
}

void statusDot(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  g->fillCircle(cx, cy, r + 3, kSurfaceHi);
  g->fillCircle(cx, cy, r, color);
}

void sparkline(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
               const float *values, uint16_t count, uint16_t tail,
               float minV, float maxV, uint16_t line, uint16_t fillUnder) {
  if (count < 2 || w < 2 || h < 2) return;

  if (minV >= maxV) {
    minV = values[0];
    maxV = values[0];
    for (uint16_t i = 1; i < count; i++) {
      if (values[i] < minV) minV = values[i];
      if (values[i] > maxV) maxV = values[i];
    }
  }
  float range = maxV - minV;
  if (range < 0.0001f) range = 1.0f;

  int16_t prevX = 0, prevY = 0;
  for (int16_t px = 0; px < w; px++) {
    float fpos = (float)px * (count - 1) / (float)(w - 1);
    uint16_t k0 = (uint16_t)fpos;
    uint16_t k1 = (k0 + 1 < count) ? k0 + 1 : k0;
    float frac = fpos - k0;
    float v0 = values[(tail + k0) % count];
    float v1 = values[(tail + k1) % count];
    float val = v0 + (v1 - v0) * frac;
    float norm = clamp01((val - minV) / range);
    int16_t py = y + (int16_t)((h - 1) * (1.0f - norm));

    if (fillUnder) {
      g->drawFastVLine(x + px, py, (y + h) - py, fillUnder);
    }
    if (px > 0) {
      g->drawLine(x + prevX, prevY, x + px, py, line);
      g->drawLine(x + prevX, prevY - 1, x + px, py - 1, line);  // 2px weight
    }
    prevX = px;
    prevY = py;
  }
}

int16_t pill(Arduino_GFX *g, int16_t x, int16_t y, const char *s,
             const GFXfont *font, uint16_t textColor, uint16_t fillColor) {
  const int16_t padX = 12;
  const int16_t H = 28;
  int16_t bx, by;
  uint16_t bw, bh;
  g->setFont(font);
  g->setTextSize(1);
  g->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  int16_t pw = (int16_t)bw + 2 * padX;
  g->fillRoundRect(x, y, pw, H, H / 2, fillColor);
  int16_t ty = y + (H - (int16_t)bh) / 2;
  text(g, x + padX, ty, s, font, textColor, kLeft);
  return pw;
}

void towerIcon(Arduino_GFX *g, int16_t x, int16_t y, uint16_t color) {
  int16_t cx = x + 14;
  // Tower body + mast.
  g->fillTriangle(cx - 7, y + 28, cx + 7, y + 28, cx, y + 11, color);
  g->fillRect(cx - 1, y + 10, 2, 18, color);
  // Beacon.
  g->fillCircle(cx, y + 8, 3, color);
  // Broadcast waves (short strokes, direction-safe - no arcs).
  g->drawLine(cx + 5, y + 3, cx + 9, y + 0, color);
  g->drawLine(cx + 6, y + 6, cx + 11, y + 4, color);
  g->drawLine(cx - 5, y + 3, cx - 9, y + 0, color);
  g->drawLine(cx - 6, y + 6, cx - 11, y + 4, color);
}

void touchButton(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
                 const char *label, bool active, uint16_t accent) {
  const uint16_t fill = active ? accent : kSurfaceHi;
  const uint16_t ink = active ? kBg : kTextHi;
  panel(g, x, y, w, h, 10, fill, 1, active ? accent : kLine);
  const GFXfont *font = fontS();
  int16_t bx, by;
  uint16_t bw, bh;
  g->setFont(font);
  g->setTextSize(1);
  g->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  text(g, x + w / 2, y + (h - (int16_t)bh) / 2, label, font, ink, kCenter);
}

void headerBar(Arduino_GFX *g, const char *title, const char *subtitle,
               const char *rightPill, uint16_t pillColor) {
  g->fillRect(0, 0, kChromeW, kChromeHeaderH, kSurface);
  g->drawFastHLine(0, kChromeHeaderH - 1, kChromeW, kLine);

  if (subtitle != nullptr) {
    text(g, 24, 12, title, fontL(), kTextHi, kLeft);
    text(g, 24, 42, subtitle, fontS(), kTextMut, kLeft);
  } else {
    text(g, 24, 24, title, fontL(), kTextHi, kLeft);
  }

  if (rightPill != nullptr) {
    const int16_t pw = textWidth(g, rightPill, fontS()) + 24;
    pill(g, kChromeW - 24 - pw, (kChromeHeaderH - 28) / 2, rightPill, fontS(), kBg, pillColor);
  }
}

void tabBar(Arduino_GFX *g, const char *const *labels, uint8_t count, uint8_t selected,
            uint16_t accent) {
  if (count == 0) return;
  g->fillRect(0, kChromeTabY, kChromeW, kChromeTabH, kSurface);
  g->drawFastHLine(0, kChromeTabY, kChromeW, kLine);

  const int16_t slot = kChromeW / count;
  for (uint8_t i = 0; i < count; i++) {
    const bool on = (i == selected);
    const int16_t cx = slot * i + slot / 2;
    text(g, cx, kChromeTabY + 20, labels[i], fontS(), on ? accent : kTextMut, kCenter);
    if (on) {
      const int16_t uw = textWidth(g, labels[i], fontS());
      g->fillRect(cx - uw / 2, kChromeTabY + 44, uw, 3, accent);
    }
  }
}

int8_t tabHit(int16_t px, int16_t py, uint8_t count) {
  if (count == 0) return -1;
  if (py < kChromeTabY || py >= kChromeH || px < 0 || px >= kChromeW) return -1;
  const int16_t slot = kChromeW / count;
  const int16_t idx = px / slot;
  return (int8_t)(idx >= count ? count - 1 : idx);
}

}  // namespace Widgets

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
