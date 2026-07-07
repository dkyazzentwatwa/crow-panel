#ifndef ADSB_RADAR_SCOPE_H
#define ADSB_RADAR_SCOPE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "AdsbTypes.h"

// The animated radar disc. Everything is composited into an offscreen RGB565
// buffer held in PSRAM, then the dashboard blits it to the panel in one shot -
// the DSI panel has a single directly-scanned framebuffer, so animating it in
// place would tear. Only compiled for USE_DISPLAY builds on the ESP32-P4.
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

class RadarScope {
 public:
  // Allocate the w x h PSRAM canvas. Returns false on allocation failure (the
  // dashboard then stays chrome-only rather than crashing).
  bool begin(int16_t w, int16_t h);

  // Compose one frame: range rings, spokes, sweep + comet-tail, and every
  // in-range aircraft. `sweepDeg` is north-up degrees. Writes each contact's
  // CANVAS-space center to outX/outY (screen-space = + the scope origin);
  // out-of-range contacts get outX = -1. selectedIdx highlights one contact.
  void render(const AdsbSnapshot &snap, float sweepDeg, int8_t selectedIdx,
              int16_t *outX, int16_t *outY);

  Arduino_GFX *canvas();       // offscreen draw device (also a valid Widgets target)
  uint16_t *framebuffer();     // tightly-packed w*h RGB565, for draw16bitRGBBitmap
  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
  bool bufferInternal() const { return bufInternal_; }  // true = fast internal SRAM

 private:
  void drawGrid_();
  void drawSweep_(float sweepDeg);
  uint16_t blipColor_(const Aircraft &a, float fade) const;

  Arduino_Canvas *fb_ = nullptr;  // concrete type is an SRAM/PSRAM subclass (see .cpp)
  bool bufInternal_ = false;
  int16_t w_ = 0, h_ = 0;
  int16_t cx_ = 0, cy_ = 0;
  int16_t rMax_ = 0;
};

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
#endif
