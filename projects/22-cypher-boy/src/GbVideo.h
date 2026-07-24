#ifndef CYPHER_BOY_VIDEO_H
#define CYPHER_BOY_VIDEO_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Scales a core's native frame onto the 1024x600 DSI panel, centred.
//
// Configured per core rather than per console: begin() takes the source size
// and integer scale, so Game Boy (160x144 x3), Genesis (320x224 x2) and NES
// (256x240 x2) all work without a line of new code here.
//
// The DSI panel is single-framebuffer, so the frame is composed off-panel first
// and then blitted - the same principle the arcade's Pong field uses. It does
// NOT use a full-size offscreen canvas: a x3 Game Boy frame is 480x432 = 405 KB,
// which does not fit the ~300 KB of internal SRAM left, and putting it in PSRAM
// would mean reading it all back out every frame.
//
// Instead one source scanline is expanded into a small `scale`-row strip
// (Game Boy: 480x3 = 5.7 KB; Genesis: 640x2 = 5 KB) in internal SRAM and
// blitted once per source row. Tiny footprint, no PSRAM read-back, and
// nearest-neighbour integer scaling keeps the pixels crisp. It also suits a
// PSRAM-resident source framebuffer (which Genesis needs at 143 KB) because it
// reads that memory linearly.
class GbVideo {
 public:
  // Allocates the scanline strip for this geometry. Safe to call again for a
  // different core; the previous strip is released first.
  // `srcStride` is the row pitch of the source buffer, which is not always the
  // visible width: the Genesis VDP always lays rows out 320 apart even when
  // only 256 pixels are shown. Defaults to srcW for cores where they match.
  bool begin(int16_t srcW, int16_t srcH, uint8_t scale, int16_t srcStride = 0);
  void blit(const uint16_t *frame);  // srcW*srcH RGB565 from the active core
  // 8-bit palette indices + a 256-entry RGB565 LUT (Genesis VDP output).
  void blitPaletted(const uint8_t *frame, const uint16_t *palette);
  void clearViewport();              // paint the viewport background

  // Configured source geometry, so the caller can notice a core changing
  // resolution mid-game.
  int16_t srcW() const { return srcW_; }
  int16_t srcH() const { return srcH_; }
  int16_t viewX() const { return vx_; }
  int16_t viewY() const { return vy_; }
  int16_t viewW() const { return (int16_t)(srcW_ * scale_); }
  int16_t viewH() const { return (int16_t)(srcH_ * scale_); }

  // Panel coordinate of a source pixel.
  int16_t pixelX(int16_t sx) const { return vx_ + sx * scale_; }
  int16_t pixelY(int16_t sy) const { return vy_ + sy * scale_; }

  // Pure placement maths, so the selftest can verify centring for every console
  // without a panel attached.
  static int16_t centreX(int16_t srcW, uint8_t scale) {
    return (int16_t)((1024 - srcW * scale) / 2);
  }
  // Below the 72px header chrome plus the 6px viewport frame.
  static int16_t topY() { return 84; }

  bool ready() const { return ready_; }

 private:
  uint16_t *strip_ = nullptr;  // (srcW*scale) wide, `scale` tall
  int16_t srcW_ = 0, srcH_ = 0, stride_ = 0;  // stride != width on Genesis
  int16_t vx_ = 0, vy_ = 0;
  uint8_t scale_ = 1;
  bool ready_ = false;
};

#endif
