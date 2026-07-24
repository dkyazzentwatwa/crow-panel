// Camera-frame renderer. COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8).
// NOT HARDWARE-VERIFIED - no frame has been observed on a physical panel.
//
// Uses the P4's PPA (Pixel-Processing Accelerator), which ships in the Arduino
// core as libesp_driver_ppa.a. The PPA reads the camera's RGB565 frame out of
// PSRAM and writes it into the DSI framebuffer, scaling on the way if the
// viewfinder rectangle is not the frame's native size. On this panel the sensor
// is 1024x600 and so is the screen, so the fullscreen case is a 1:1 DMA copy
// with no scaling at all.

#include "CamRenderer.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <driver/ppa.h>
#include <esp_cache.h>

namespace {

ppa_client_handle_t gPpa = nullptr;

// The DSI framebuffer, fetched once. Arduino_DSI_Display owns it; we only write
// pixels into it, and CrowDisplay::flush() is what makes them visible.
uint16_t *framebuffer() {
  Arduino_GFX *gfx = CrowDisplay::canvas();
  if (gfx == nullptr) return nullptr;
  return static_cast<Arduino_DSI_Display *>(gfx)->getFramebuffer();
}

constexpr int16_t kPanelW = 1024;
constexpr int16_t kPanelH = 600;

// Clamps a target rectangle to the panel. A rectangle that runs off the edge
// would otherwise have the PPA writing past the framebuffer.
bool clampRect(int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  if (w <= 0 || h <= 0) return false;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= kPanelW || y >= kPanelH) return false;
  if (x + w > kPanelW) w = kPanelW - x;
  if (y + h > kPanelH) h = kPanelH - y;
  return w > 0 && h > 0;
}

}  // namespace

bool CamRenderer::begin() {
  if (gPpa != nullptr) {
    ppaReady_ = true;
    return true;
  }

  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  // One pending transaction is enough: drawFrame blocks until the blit lands,
  // because the next thing that happens is a flush of the same rectangle and
  // flushing a half-written frame is exactly the tearing we are avoiding.
  config.max_pending_trans_num = 1;

  if (ppa_register_client(&config, &gPpa) != ESP_OK) {
    gPpa = nullptr;
    ppaReady_ = false;
    lastError_ = "PPA unavailable; falling back to CPU blit";
    Logger::warn("renderer", lastError_);
    return false;
  }

  ppaReady_ = true;
  lastError_ = "";
  Logger::info("renderer", "PPA hardware blitter ready");
  return true;
}

bool CamRenderer::drawFrameCpu_(const CrowCamera::Frame &frame, int16_t x, int16_t y,
                                int16_t w, int16_t h) {
  uint16_t *fb = framebuffer();
  if (fb == nullptr) return false;

  // Nearest-neighbour, because this path exists to keep something on screen
  // when the hardware blitter is missing, not to look good doing it.
  for (int16_t row = 0; row < h; row++) {
    const uint32_t srcY = ((uint32_t)row * frame.height) / (uint32_t)h;
    const uint16_t *src = frame.data + srcY * frame.width;
    uint16_t *dst = fb + (uint32_t)(y + row) * kPanelW + x;
    if (w == (int16_t)frame.width) {
      memcpy(dst, src, (size_t)w * sizeof(uint16_t));
    } else {
      for (int16_t col = 0; col < w; col++) {
        dst[col] = src[((uint32_t)col * frame.width) / (uint32_t)w];
      }
    }
  }
  return true;
}

bool CamRenderer::drawFrame(const CrowCamera::Frame &frame, int16_t x, int16_t y,
                            int16_t w, int16_t h) {
  if (frame.data == nullptr) return false;
  if (!clampRect(x, y, w, h)) return false;

  uint16_t *fb = framebuffer();
  if (fb == nullptr) {
    lastError_ = "no framebuffer; was CrowDisplay::begin() called?";
    return false;
  }

  const uint32_t startUs = micros();
  bool drawn = false;

  if (ppaReady_ && gPpa != nullptr) {
    ppa_srm_oper_config_t op = {};

    op.in.buffer = frame.data;
    op.in.pic_w = frame.width;
    op.in.pic_h = frame.height;
    op.in.block_w = frame.width;
    op.in.block_h = frame.height;
    op.in.block_offset_x = 0;
    op.in.block_offset_y = 0;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    // The output "picture" is the whole framebuffer; the block is the
    // viewfinder rectangle inside it. Getting pic_w wrong here is the classic
    // way to produce a sheared image - it is the framebuffer stride, not the
    // width of the region being written.
    op.out.buffer = fb;
    op.out.buffer_size = (uint32_t)kPanelW * kPanelH * sizeof(uint16_t);
    op.out.pic_w = kPanelW;
    op.out.pic_h = kPanelH;
    op.out.block_offset_x = x;
    op.out.block_offset_y = y;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    op.scale_x = (float)w / (float)frame.width;
    op.scale_y = (float)h / (float)frame.height;
    op.mirror_x = false;
    op.mirror_y = false;
    // Mirroring is deliberately NOT done here: the sensor can flip in hardware
    // for free (Sc2336Sensor::setFlip), so spending PPA bandwidth on it would
    // be waste. This stays false and the UI drives the sensor instead.
    op.rgb_swap = false;
    op.byte_swap = false;
    op.mode = PPA_TRANS_MODE_BLOCKING;

    drawn = (ppa_do_scale_rotate_mirror(gPpa, &op) == ESP_OK);
    if (!drawn) {
      // A failed blit is worth knowing about, but not worth losing the frame
      // over - drop to the CPU path for this one.
      lastError_ = "PPA blit failed; used CPU fallback";
      drawn = drawFrameCpu_(frame, x, y, w, h);
    }
  } else {
    drawn = drawFrameCpu_(frame, x, y, w, h);
  }

  if (!drawn) return false;

  // Flush ONLY the viewfinder rectangle. Flushing the whole panel here would
  // cache-sync 1.2 MB per frame and re-push chrome that has not changed.
  CrowDisplay::flush(x, y, w, h);

  lastBlitUs_ = micros() - startUs;
  blitTotalUs_ += lastBlitUs_;
  blitSamples_++;
  // Keep the rolling average recent rather than lifetime-cumulative, so a slow
  // patch shows up instead of being diluted by every frame since boot.
  if (blitSamples_ >= 120) {
    blitTotalUs_ = averageBlitUs() * 30;
    blitSamples_ = 30;
  }
  return true;
}

#else  // !USE_DISPLAY or not a P4

bool CamRenderer::begin() {
  lastError_ = "built without USE_DISPLAY";
  return false;
}

bool CamRenderer::drawFrame(const CrowCamera::Frame &, int16_t, int16_t, int16_t, int16_t) {
  return false;
}

#endif
