#include "GbVideo.h"

#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include "esp_heap_caps.h"
#endif

bool GbVideo::begin(int16_t srcW, int16_t srcH, uint8_t scale, int16_t srcStride) {
  srcW_ = srcW;
  srcH_ = srcH;
  stride_ = srcStride > 0 ? srcStride : srcW;
  scale_ = scale ? scale : 1;
  vx_ = centreX(srcW_, scale_);
  vy_ = topY();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // Re-allocate: a different core means a different strip width.
  if (strip_) {
    heap_caps_free(strip_);
    strip_ = nullptr;
  }
  ready_ = false;
  const size_t px = (size_t)srcW_ * scale_ * scale_;
  strip_ = (uint16_t *)heap_caps_malloc(px * sizeof(uint16_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!strip_) {
    Logger::error("gbvideo", "scanline strip alloc failed");
    return false;
  }
  ready_ = true;
  Logger::info("gbvideo", String("viewport ") + viewW() + "x" + viewH() + " at " +
                              vx_ + "," + vy_ + " (x" + scale_ + ")");
  return true;
#else
  ready_ = false;
  return false;
#endif
}

void GbVideo::clearViewport() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  // A 2px frame around the screen so the viewport reads as a "cartridge screen"
  // rather than floating pixels.
  g->fillRect(vx_ - 6, vy_ - 6, viewW() + 12, viewH() + 12, Widgets::kSurfaceHi);
  g->drawRect(vx_ - 6, vy_ - 6, viewW() + 12, viewH() + 12, Widgets::kLine);
  g->fillRect(vx_, vy_, viewW(), viewH(), Widgets::kBg);
  CrowDisplay::flush(vx_ - 6, vy_ - 6, viewW() + 12, viewH() + 12);
#endif
}

void GbVideo::blitPaletted(const uint8_t *frame, const uint16_t *palette) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_ || !frame || !palette || srcW_ <= 0) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t outW = viewW();

  for (int16_t y = 0; y < srcH_; y++) {
    const uint8_t *src = frame + (size_t)y * stride_;  // pitch != width on Genesis
    uint16_t *row0 = strip_;
    for (int16_t x = 0; x < srcW_; x++) {
      const uint16_t c = palette[src[x]];  // index -> RGB565, no extra buffer
      uint16_t *dst = row0 + (size_t)x * scale_;
      for (uint8_t s = 0; s < scale_; s++) dst[s] = c;
    }
    for (uint8_t r = 1; r < scale_; r++) {
      memcpy(strip_ + (size_t)r * outW, row0, (size_t)outW * sizeof(uint16_t));
    }
    g->draw16bitRGBBitmap(vx_, vy_ + y * scale_, strip_, outW, scale_);
  }
  CrowDisplay::flush(vx_, vy_, outW, viewH());
#else
  (void)frame;
  (void)palette;
#endif
}

void GbVideo::blit(const uint16_t *frame) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_ || !frame || srcW_ <= 0) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  const int16_t outW = viewW();

  for (int16_t y = 0; y < srcH_; y++) {
    const uint16_t *src = frame + (size_t)y * stride_;

    // Expand one source scanline horizontally into the first strip row...
    uint16_t *row0 = strip_;
    for (int16_t x = 0; x < srcW_; x++) {
      const uint16_t c = src[x];
      uint16_t *dst = row0 + (size_t)x * scale_;
      for (uint8_t s = 0; s < scale_; s++) dst[s] = c;
    }
    // ...then duplicate it down to fill the strip vertically.
    for (uint8_t r = 1; r < scale_; r++) {
      memcpy(strip_ + (size_t)r * outW, row0, (size_t)outW * sizeof(uint16_t));
    }

    g->draw16bitRGBBitmap(vx_, vy_ + y * scale_, strip_, outW, scale_);
  }

  // One cache sync for the whole viewport rather than one per source row.
  CrowDisplay::flush(vx_, vy_, outW, viewH());
#else
  (void)frame;
#endif
}
