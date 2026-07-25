#ifndef VISION_CAM_RENDERER_H
#define VISION_CAM_RENDERER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

// Puts camera frames on the panel.
//
// The hard constraint this class exists to respect: the MIPI-DSI panel has ONE
// framebuffer and no page flip. Anything drawn is visible immediately, so a
// naive "clear, blit frame, draw chrome, flush everything" loop tears badly at
// video rates. Two things follow, and they shape the whole design:
//
//   1. The frame goes to the panel via the PPA - the P4's hardware 2D blitter -
//      writing straight into the framebuffer Arduino_GFX owns. The CPU never
//      touches 1.2 MB of pixels. A CPU row-copy fallback exists for when PPA
//      registration fails, and it says so rather than silently crawling.
//   2. Only the viewfinder rectangle is flushed per frame. Chrome (HUD, tabs)
//      lives outside that rectangle and is flushed only when it changes, which
//      is what keeps the panel's single framebuffer usable for video at all.
//
// The panel must have been built with manualFlush=true for this to hold - see
// CrowDisplay::begin's third argument in the sketch.

class CamRenderer {
 public:
  // Registers the PPA client. Safe to call when USE_DISPLAY is off (no-op).
  // Returns false if the hardware blitter is unavailable; the renderer still
  // works via the CPU path in that case, so the return is informational.
  bool begin();

  // Blits one frame into the framebuffer rectangle (x, y, w, h). Scales if the
  // frame does not match the rectangle, which the PPA does for free. Returns
  // false if nothing was drawn.
  //
  // `autoFlush` false writes the framebuffer WITHOUT pushing it, leaving the
  // caller to flush. That matters whenever chrome is drawn over the image:
  // flushing the full frame first and the chrome second pushes video into the
  // chrome's rectangle for one frame, every frame, which reads as a persistent
  // flicker rather than the one-off it looks like in code.
  bool drawFrame(const CrowCamera::Frame &frame, int16_t x, int16_t y, int16_t w,
                 int16_t h, bool autoFlush = true);

  // True when the hardware blitter is in use. The UI surfaces this because a
  // silent fall back to the CPU path is the difference between 25 fps and 4.
  bool hardwareAccelerated() const { return ppaReady_; }

  // Microseconds the last drawFrame() spent, and a rolling average. Reported on
  // screen so the cost of the blit is visible rather than guessed at.
  uint32_t lastBlitUs() const { return lastBlitUs_; }
  uint32_t averageBlitUs() const { return blitSamples_ ? blitTotalUs_ / blitSamples_ : 0; }

  const char *lastError() const { return lastError_; }

 private:
  bool ppaReady_ = false;
  uint32_t lastBlitUs_ = 0;
  uint32_t blitTotalUs_ = 0;
  uint32_t blitSamples_ = 0;
  const char *lastError_ = "not started";

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // Straight memcpy per row into the framebuffer. Correct but slow; only used
  // when the PPA is unavailable or the frame already matches the target size.
  bool drawFrameCpu_(const CrowCamera::Frame &frame, int16_t x, int16_t y, int16_t w, int16_t h);
#endif
};

#endif
