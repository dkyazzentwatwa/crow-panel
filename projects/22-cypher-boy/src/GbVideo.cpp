#include "GbVideo.h"

#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include "esp_heap_caps.h"
#endif

bool GbVideo::begin() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (ready_) return true;
  const size_t px = (size_t)GB_W * GB_SCALE * GB_SCALE;  // 480 * 3
  strip_ = (uint16_t *)heap_caps_malloc(px * sizeof(uint16_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!strip_) {
    Logger::error("gbvideo", "scanline strip alloc failed");
    return false;
  }
  ready_ = true;
  Logger::info("gbvideo", String("viewport ") + viewW() + "x" + viewH() + " at " +
                              GB_VIEW_X + "," + GB_VIEW_Y + " (x" + GB_SCALE + ")");
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
  g->fillRect(GB_VIEW_X - 6, GB_VIEW_Y - 6, viewW() + 12, viewH() + 12, Widgets::kSurfaceHi);
  g->drawRect(GB_VIEW_X - 6, GB_VIEW_Y - 6, viewW() + 12, viewH() + 12, Widgets::kLine);
  g->fillRect(GB_VIEW_X, GB_VIEW_Y, viewW(), viewH(), Widgets::kBg);
  CrowDisplay::flush(GB_VIEW_X - 6, GB_VIEW_Y - 6, viewW() + 12, viewH() + 12);
#endif
}

void GbVideo::blit(const uint16_t *frame) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_ || !frame) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  const int16_t outW = viewW();  // GB_W * GB_SCALE

  for (int16_t y = 0; y < GB_H; y++) {
    const uint16_t *src = frame + (size_t)y * GB_W;

    // Expand one GB scanline horizontally into the first strip row...
    uint16_t *row0 = strip_;
    for (int16_t x = 0; x < GB_W; x++) {
      const uint16_t c = src[x];
      uint16_t *dst = row0 + (size_t)x * GB_SCALE;
      for (int16_t s = 0; s < GB_SCALE; s++) dst[s] = c;
    }
    // ...then duplicate it down to fill the strip vertically.
    for (int16_t r = 1; r < GB_SCALE; r++) {
      memcpy(strip_ + (size_t)r * outW, row0, (size_t)outW * sizeof(uint16_t));
    }

    g->draw16bitRGBBitmap(GB_VIEW_X, GB_VIEW_Y + y * GB_SCALE, strip_, outW, GB_SCALE);
  }

  // One cache sync for the whole viewport rather than 144 small ones.
  CrowDisplay::flush(GB_VIEW_X, GB_VIEW_Y, outW, viewH());
#else
  (void)frame;
#endif
}
