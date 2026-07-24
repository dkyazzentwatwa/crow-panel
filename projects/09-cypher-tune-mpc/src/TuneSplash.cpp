#include "TuneSplash.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>
#include <math.h>
#include "UiLayout.h"

namespace {

using namespace UiLayout;

// Phase lengths, ms. Total ~2.3 s: long enough to read, short enough that it
// never feels like it is in the way of playing.
constexpr uint16_t kFadeMs = 520;
constexpr uint16_t kSweepMs = 900;
constexpr uint16_t kGridMs = 620;
constexpr uint16_t kHoldMs = 260;
constexpr uint8_t kFrameMs = 16;  // ~60 fps pacing

// Waveform band: centered under the wordmark, spanning most of the width.
constexpr int16_t kWaveX = 92;
constexpr int16_t kWaveW = 840;
constexpr int16_t kWaveY = 330;   // centerline
constexpr int16_t kWaveAmp = 88;

constexpr int16_t kTitleY = 196;
constexpr int16_t kSubtitleY = 404;

uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
  int16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int16_t r = ar + (((br - ar) * t) >> 8);
  int16_t g = ag + (((bg - ag) * t) >> 8);
  int16_t bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// The swept shape: two detuned sines under an exponential decay, i.e. what a
// struck drum actually looks like. `phase` is 0..1 across the band.
int16_t waveY(float phase) {
  float env = expf(-3.2f * phase);
  float body = sinf(phase * 46.0f) * 0.75f + sinf(phase * 23.0f) * 0.25f;
  return (int16_t)(body * env * kWaveAmp);
}

void drawTitle(Arduino_GFX *g, const TuneTheme &t, uint8_t level) {
  uint16_t ink = blend(t.bg, t.ink, level);
  uint16_t accent = blend(t.bg, t.accent, level);
  Widgets::text(g, kScreenW / 2, kTitleY, "CYPHER TUNE", Widgets::fontXL(), ink,
                Widgets::kCenter);
  Widgets::text(g, kScreenW / 2, kTitleY + 62, "T O U C H   G R O O V E B O X",
                Widgets::fontS(), accent, Widgets::kCenter);
}

}  // namespace

namespace TuneSplash {

void run(const TuneTheme &t, const char *subtitle) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) {
    return;
  }

  g->fillScreen(t.bg);
  CrowDisplay::flush();

  // Phase 1: wordmark fades up out of the background.
  for (uint16_t elapsed = 0; elapsed < kFadeMs; elapsed += kFrameMs) {
    uint8_t level = (uint8_t)((uint32_t)elapsed * 255 / kFadeMs);
    drawTitle(g, t, level);
    CrowDisplay::flush(0, kTitleY - 10, kScreenW, 90);
    delay(kFrameMs);
  }
  drawTitle(g, t, 255);
  CrowDisplay::flush(0, kTitleY - 10, kScreenW, 90);

  // Phase 2: the waveform draws itself left to right, a bright head pulling
  // the trace behind it. Only the newly revealed columns are drawn each frame,
  // so the cost per frame stays tiny no matter how wide the band is.
  g->drawFastHLine(kWaveX, kWaveY, kWaveW, blend(t.bg, t.line, 200));
  CrowDisplay::flush(0, kWaveY - 2, kScreenW, 4);
  int16_t drawn = 0;
  for (uint16_t elapsed = 0; elapsed < kSweepMs; elapsed += kFrameMs) {
    int16_t target = (int16_t)((uint32_t)elapsed * kWaveW / kSweepMs);
    if (target > kWaveW) {
      target = kWaveW;
    }
    if (target <= drawn) {
      delay(kFrameMs);
      continue;
    }
    int16_t top = kWaveY - kWaveAmp - 4;
    for (int16_t col = drawn; col < target; col++) {
      float phase = (float)col / (float)kWaveW;
      int16_t dy = waveY(phase);
      int16_t y0 = dy >= 0 ? kWaveY - dy : kWaveY;
      int16_t h = (dy >= 0 ? dy : -dy) + 1;
      // Trace body, then a hotter core near the head for a "live" look.
      g->drawFastVLine(kWaveX + col, y0, h, t.accent);
      if (target - col < 26) {
        uint8_t heat = (uint8_t)(255 - (target - col) * 9);
        g->drawFastVLine(kWaveX + col, y0, h, blend(t.accent, t.padFlash, heat));
      }
    }
    // Leading-edge marker.
    g->drawFastVLine(kWaveX + target, kWaveY - kWaveAmp, kWaveAmp * 2, t.padFlash);
    drawn = target;
    CrowDisplay::flush(0, top, kScreenW, kWaveAmp * 2 + 8);
    delay(kFrameMs);
  }
  // Erase the leading-edge marker now that the sweep has landed.
  g->drawFastVLine(kWaveX + kWaveW, kWaveY - kWaveAmp, kWaveAmp * 2, t.bg);
  if (subtitle != nullptr && subtitle[0] != '\0') {
    Widgets::text(g, kScreenW / 2, kSubtitleY, subtitle, Widgets::fontS(),
                  t.muted, Widgets::kCenter);
  }
  CrowDisplay::flush(0, kWaveY - kWaveAmp - 4, kScreenW, kWaveAmp * 2 + 100);

  // Phase 3: the pad grid wipes in, one row at a time from the bottom, each
  // row's cells staggered left to right - the instrument arriving under the
  // waveform it just drew.
  uint16_t perCell = kGridMs / 16;
  if (perCell == 0) {
    perCell = 1;
  }
  for (int8_t row = 3; row >= 0; row--) {
    for (uint8_t col = 0; col < 4; col++) {
      int16_t x = padX(col);
      int16_t y = padY((uint8_t)row);
      g->fillRoundRect(x, y, kPadCellW, kPadCellH, 10,
                       (row % 2 == 0) ? t.padFill : t.padFillAlt);
      g->drawRoundRect(x, y, kPadCellW, kPadCellH, 10, t.accent);
      CrowDisplay::flush(x, y, kPadCellW, kPadCellH);
      delay(perCell);
    }
  }

  delay(kHoldMs);
}

}  // namespace TuneSplash

#else  // no display: nothing to animate

namespace TuneSplash {
void run(const TuneTheme &, const char *) {}
}  // namespace TuneSplash

#endif
