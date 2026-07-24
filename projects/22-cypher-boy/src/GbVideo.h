#ifndef CYPHER_BOY_VIDEO_H
#define CYPHER_BOY_VIDEO_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Scales the 160x144 Game Boy frame onto the 1024x600 DSI panel.
//
// The DSI panel is single-framebuffer, so the frame is composed off-panel
// first and then blitted - the same principle the arcade's Pong field uses.
// It does NOT use a full-size offscreen canvas though: a x3 frame is
// 480x432 = 405 KB, which does not fit the ~300 KB of internal SRAM left, and
// putting it in PSRAM would mean reading 405 KB back out every frame.
//
// Instead a single GB scanline is expanded into a small GB_SCALE-row strip
// (480x3 = 5.7 KB, internal SRAM) and blitted, 144 times per frame. Tiny
// footprint, no PSRAM read-back, and nearest-neighbour integer scaling keeps
// the pixels crisp.
class GbVideo {
 public:
  bool begin();
  void blit(const uint16_t *frame);  // 160x144 RGB565 from GameBoyHost
  void clearViewport();              // paint the viewport background

  // Panel coordinates of a Game Boy pixel. Pure maths, so the selftest can
  // assert the scaling without a panel attached.
  static int16_t viewX(int16_t gx) { return GB_VIEW_X + gx * GB_SCALE; }
  static int16_t viewY(int16_t gy) { return GB_VIEW_Y + gy * GB_SCALE; }
  static int16_t viewW() { return GB_W * GB_SCALE; }
  static int16_t viewH() { return GB_H * GB_SCALE; }

  bool ready() const { return ready_; }

 private:
  uint16_t *strip_ = nullptr;  // GB_W*GB_SCALE wide, GB_SCALE tall
  bool ready_ = false;
};

#endif
