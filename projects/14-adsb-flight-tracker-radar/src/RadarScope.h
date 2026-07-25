#ifndef ADSB_RADAR_SCOPE_H
#define ADSB_RADAR_SCOPE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "AdsbTypes.h"

// The animated radar disc. Everything is composited into an offscreen RGB565
// buffer (internal SRAM when it fits, PSRAM otherwise - see the .cpp), then the
// dashboard blits it to the panel in one shot: the DSI panel has a single
// directly-scanned framebuffer, so animating it in place would tear. The
// selected-aircraft detail card is composited into the SAME buffer for the same
// reason - it overlaps the disc, so drawing it separately meant the blit erased
// it and the repaint re-drew it 30x a second, which is what made it strobe.
// Only compiled for USE_DISPLAY builds on the ESP32-P4.
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

// One altitude band: the colour AND the legend label, so the scope blip, the
// list dot, the detail-card border and the on-screen legend can never drift
// apart. They used to: altColor() and blipColor_() were separate ladders with
// different palettes, so the same aircraft was green in the list and a
// different green on the disc.
struct AltBand {
  const char *label;
  uint16_t color;
};

class RadarScope {
 public:
  // Allocate the w x h offscreen canvas. Returns false on allocation failure
  // (the dashboard then stays chrome-only rather than crashing).
  bool begin(int16_t w, int16_t h);

  // Compose one frame: bezel, range rings, spokes, sweep + comet-tail, and
  // every in-range aircraft. `sweepDeg` is north-up degrees. Writes each
  // contact's CANVAS-space center to outX/outY (screen-space = + the scope
  // origin); out-of-range contacts get outX = -1. selectedIdx highlights one
  // contact. Pass cardOpen so the background clear can skip the rows
  // renderDetail() is about to paint over anyway.
  void render(const AdsbSnapshot &snap, float sweepDeg, int8_t selectedIdx,
              int16_t *outX, int16_t *outY, bool cardOpen = false);

  // Composite the selected-aircraft card INTO the canvas. Must be called after
  // render() (which clears) and before the dashboard blits, so the card and the
  // disc reach the panel in one draw16bitRGBBitmap.
  void renderDetail(const Aircraft &a);

  Arduino_GFX *canvas();       // offscreen draw device (also a valid Widgets target)
  uint16_t *framebuffer();     // tightly-packed w*h RGB565, for draw16bitRGBBitmap
  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
  bool bufferInternal() const { return bufInternal_; }  // true = fast internal SRAM

  // --- Shared presentation helpers (static: the dashboard draws the list rows
  // and the legend against the exact same ramp and the same arrow primitive). ---
  static uint8_t altBandCount();
  static const AltBand &altBandAt(uint8_t i);
  static const AltBand &altBand(const Aircraft &a);
  static uint16_t altBandColor(const Aircraft &a) { return altBand(a).color; }
  static uint16_t fadeColor(uint16_t c565, float fade);

  // Filled direction triangle pointing at `deg` (0 = north, clockwise). Used
  // for blips, list-row bearings and the detail card's heading rosette.
  static void arrow(Arduino_GFX *g, int16_t cx, int16_t cy, float deg, float r, uint16_t color);

  // Truncate with an ellipsis until the string fits maxW pixels in `font`.
  static String fit(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW);

  // Canvas-space detail-card rect (screen-space = + the scope origin).
  static constexpr int16_t kCardX = 12;
  static constexpr int16_t kCardY = 64;
  static constexpr int16_t kCardW = 336;
  static constexpr int16_t kCardH = 232;

 private:
  void drawGrid_();
  void drawBezel_();
  void drawSweep_(float sweepDeg);
  void drawRangeLabels_(int km);

  Arduino_Canvas *fb_ = nullptr;  // concrete type is an SRAM/PSRAM subclass (see .cpp)
  bool bufInternal_ = false;
  int16_t w_ = 0, h_ = 0;
  int16_t cx_ = 0, cy_ = 0;
  int16_t rMax_ = 0;
};

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
#endif
