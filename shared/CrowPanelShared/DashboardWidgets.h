#ifndef CROW_PANEL_DASHBOARD_WIDGETS_H
#define CROW_PANEL_DASHBOARD_WIDGETS_H

#include <Arduino.h>
#include "AppConfig.h"

// Reusable dashboard drawing toolkit for the CrowPanel's 1024x600 DSI panel.
// Everything draws through an Arduino_GFX device (the Adafruit-GFX-style
// API) so all three projects can compose pro dashboards from the same
// primitives. Compiled only for USE_DISPLAY builds on the ESP32-P4.
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

// Pulls in the Arduino_GFX class and the GFXfont typedef. GFXfont is a
// typedef of an anonymous struct, so it cannot be forward-declared - the
// real header is required wherever these signatures are visible.
#include <Arduino_GFX_Library.h>

namespace Widgets {

// --- RGB565 "operations" palette (dark). constexpr, no device needed. ---
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kBg        = rgb(0x0B, 0x11, 0x1C);  // page background
constexpr uint16_t kSurface   = rgb(0x16, 0x20, 0x2F);  // card fill
constexpr uint16_t kSurfaceHi = rgb(0x1E, 0x2B, 0x3D);  // highlighted card
constexpr uint16_t kLine      = rgb(0x2A, 0x3A, 0x4F);  // borders / gauge track
constexpr uint16_t kTextHi    = rgb(0xEA, 0xF0, 0xF7);  // primary text
constexpr uint16_t kTextMut   = rgb(0x82, 0x96, 0xAC);  // secondary text
constexpr uint16_t kAccent    = rgb(0x16, 0xC2, 0xC9);  // teal accent
constexpr uint16_t kGreen     = rgb(0x35, 0xD0, 0x7F);  // ok
constexpr uint16_t kAmber     = rgb(0xF7, 0xB7, 0x33);  // warning
constexpr uint16_t kRed       = rgb(0xFF, 0x54, 0x70);  // danger

// --- Fonts (vendored FreeSans; see fonts/README.md). ---
const GFXfont *fontXL();  // FreeSansBold 24pt - hero gauge values
const GFXfont *fontL();   // FreeSansBold 12pt - titles
const GFXfont *fontM();   // FreeSans     12pt - body values
const GFXfont *fontS();   // FreeSansBold  9pt - labels / footer

enum Align { kLeft = 0, kCenter = 1, kRight = 2 };

// Draw text with a custom font. `y` is the TOP of the glyph box; for
// kLeft `x` is the left edge, kCenter `x` is the horizontal center,
// kRight `x` is the right edge.
void text(Arduino_GFX *g, int16_t x, int16_t y, const char *s,
          const GFXfont *font, uint16_t color, Align align = kLeft);

// Pixel width a string would occupy in `font` (for manual layout).
int16_t textWidth(Arduino_GFX *g, const char *s, const GFXfont *font);

// Rounded card. Pass border > 0 to stroke it in borderColor.
void panel(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
           int16_t radius, uint16_t fill, int16_t border = 0, uint16_t borderColor = kLine);

// 270-degree ring gauge (gap at the bottom). value01 clamps to [0,1].
// Draws the track then the value arc; the caller draws centered text.
void arcGauge(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t rOuter, int16_t rInner,
              float value01, uint16_t valueColor, uint16_t trackColor = kLine);

// Rounded horizontal progress bar.
void hBar(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
          float value01, uint16_t fill, uint16_t track = kLine);

// 4-bar signal-strength glyph, `level` filled bars (0-4).
void signalBars(Arduino_GFX *g, int16_t x, int16_t baselineY, uint8_t level,
                uint16_t on, uint16_t off = kLine);

// Filled status dot with a faint halo.
void statusDot(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t r, uint16_t color);

// Line chart of `count` samples stored in a ring buffer whose oldest entry
// is at index `tail`. Auto-ranges when minV >= maxV.
void sparkline(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
               const float *values, uint16_t count, uint16_t tail,
               float minV, float maxV, uint16_t line, uint16_t fillUnder = 0);

// Rounded label chip. Returns its total width so chips can be chained.
int16_t pill(Arduino_GFX *g, int16_t x, int16_t y, const char *s,
             const GFXfont *font, uint16_t textColor, uint16_t fillColor);

// Small signal-tower glyph (mast + broadcast arcs), ~28x28 at (x,y) top-left.
void towerIcon(Arduino_GFX *g, int16_t x, int16_t y, uint16_t color);

// --- Touch-UI chrome. Layout constants match the 1024x600 DSI panel. ---
//
// Names carry a kChrome/touch/Rect prefix on purpose: seven projects pull this
// namespace in with `using namespace Widgets`, and bare names like kChromeW,
// kChromeHeaderH, button and hit are already taken by project-local constants.

constexpr int16_t kChromeW = 1024;
constexpr int16_t kChromeH = 600;
constexpr int16_t kChromeHeaderH = 72;  // headerBar() height
constexpr int16_t kChromeTabH = 64;     // tabBar() height
constexpr int16_t kChromeTabY = kChromeH - kChromeTabH;

// Point-in-rect test for hit-testing touch releases against a control.
inline bool hitRect(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return px >= x && px < (int16_t)(x + w) && py >= y && py < (int16_t)(y + h);
}

// Rounded tappable button. `active` swaps the accent fill in (pressed or
// selected state) for the muted surface fill.
void touchButton(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
                 const char *label, bool active, uint16_t accent = kAccent);

// Full-width top chrome: title on the left, subtitle beneath it, and an
// optional status pill on the right. Pass nullptr to omit subtitle/pill.
void headerBar(Arduino_GFX *g, const char *title, const char *subtitle = nullptr,
               const char *rightPill = nullptr, uint16_t pillColor = kAccent);

// Full-width bottom navigation strip of evenly-spaced tabs; `selected` is
// drawn in the accent color with an underline.
void tabBar(Arduino_GFX *g, const char *const *labels, uint8_t count, uint8_t selected,
            uint16_t accent = kAccent);

// Which tab a touch at (px,py) landed on, or -1 if the point misses the strip.
int8_t tabHit(int16_t px, int16_t py, uint8_t count);

}  // namespace Widgets

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
#endif
