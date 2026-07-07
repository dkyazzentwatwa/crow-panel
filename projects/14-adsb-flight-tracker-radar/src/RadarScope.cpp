#include "RadarScope.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <CrowPanelShared.h>  // Widgets:: + fonts
#include <math.h>
#include <stdio.h>
#include "esp_heap_caps.h"

using namespace Widgets;

namespace {
// Radar green ramp + near-black scope background.
const uint16_t kScope = rgb(5, 12, 18);
const uint16_t kGDark = rgb(0, 46, 22);
const uint16_t kGDim = rgb(0, 92, 40);
const uint16_t kGMed = rgb(0, 168, 60);
const uint16_t kGBright = rgb(0, 255, 70);

// Offscreen scope buffer. We inject it before any draw so Arduino_Canvas::begin()
// skips its own (internal-SRAM aligned_alloc) allocation. The per-frame recompose
// + blit is bandwidth-heavy, and INTERNAL SRAM is ~10x faster than PSRAM here, so
// we allocate there first (the scope is kept small enough to fit) and fall back to
// PSRAM only if it doesn't. `internal()` reports which, for the boot log.
class RadarCanvas : public Arduino_Canvas {
 public:
  RadarCanvas(int16_t w, int16_t h) : Arduino_Canvas(w, h, nullptr) {}
  bool alloc() {
    if (_framebuffer) return true;
    size_t sz = (size_t)WIDTH * HEIGHT * 2;
    _framebuffer = (uint16_t *)heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    internal_ = (_framebuffer != nullptr);
    if (!_framebuffer) _framebuffer = (uint16_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    return _framebuffer != nullptr;
  }
  bool internal() const { return internal_; }

 private:
  bool internal_ = false;
};
}  // namespace

bool RadarScope::begin(int16_t w, int16_t h) {
  w_ = w & ~1;  // even width => the uint32 fast-copy path in the blit engages
  h_ = h;
  cx_ = w_ / 2;
  cy_ = h_ / 2;
  rMax_ = (cx_ < cy_ ? cx_ : cy_) - 18;

  RadarCanvas *c = new RadarCanvas(w_, h_);
  if (!c->alloc()) {
    delete c;
    fb_ = nullptr;
    return false;
  }
  bufInternal_ = c->internal();
  fb_ = c;
  return true;
}

Arduino_GFX *RadarScope::canvas() { return fb_; }
uint16_t *RadarScope::framebuffer() { return fb_ ? fb_->getFramebuffer() : nullptr; }

uint16_t RadarScope::blipColor_(const Aircraft &a, float fade) const {
  uint8_t r, g, b;
  if (!a.haveAlt) {
    r = 255; g = 40; b = 40;          // unknown altitude
  } else if (a.altFt >= 30000) {
    r = 0; g = 180; b = 255;          // high
  } else if (a.altFt >= 10000) {
    r = 0; g = 255; b = 35;           // mid
  } else {
    r = 255; g = 220; b = 0;          // low
  }
  return rgb((uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
}

void RadarScope::drawGrid_() {
  Arduino_GFX *g = fb_;
  for (int i = 1; i <= 5; i++) {
    g->drawCircle(cx_, cy_, rMax_ * i / 5, (i == 5) ? kGMed : kGDark);
  }
  g->drawCircle(cx_, cy_, rMax_ + 3, kGDim);  // outer bezel

  for (int a = 0; a < 360; a += 30) {         // spokes
    float rad = (a - 90) * DEG_TO_RAD;
    g->drawLine(cx_, cy_, cx_ + (int)(cosf(rad) * rMax_), cy_ + (int)(sinf(rad) * rMax_), kGDark);
  }
  g->drawLine(cx_, cy_ - rMax_, cx_, cy_ + rMax_, kGDim);  // N-S
  g->drawLine(cx_ - rMax_, cy_, cx_ + rMax_, cy_, kGDim);  // E-W

  text(g, cx_, cy_ - rMax_ - 14, "N", fontS(), kGBright, kCenter);
  text(g, cx_, cy_ + rMax_ + 2, "S", fontS(), kGDim, kCenter);
  text(g, cx_ + rMax_ + 5, cy_ - 5, "E", fontS(), kGDim, kLeft);
  text(g, cx_ - rMax_ - 5, cy_ - 5, "W", fontS(), kGDim, kRight);
}

void RadarScope::drawSweep_(float sweepDeg) {
  Arduino_GFX *g = fb_;
  const float trail = 60.0f;  // degrees of fading comet-tail behind the edge
  for (float i = trail; i >= 0.0f; i -= 1.5f) {
    float a = sweepDeg - i;
    float intensity = (trail - i) / trail;  // 0 at the tail, 1 at the edge
    uint16_t col = rgb(0, (uint8_t)(6 + intensity * 120), 0);
    float rad = (a - 90.0f) * DEG_TO_RAD;
    g->drawLine(cx_, cy_, cx_ + (int16_t)(cosf(rad) * rMax_), cy_ + (int16_t)(sinf(rad) * rMax_), col);
  }
  float rad = (sweepDeg - 90.0f) * DEG_TO_RAD;  // bright leading edge
  g->drawLine(cx_, cy_, cx_ + (int16_t)(cosf(rad) * rMax_), cy_ + (int16_t)(sinf(rad) * rMax_), kGBright);
}

void RadarScope::render(const AdsbSnapshot &snap, float sweepDeg, int8_t selectedIdx,
                        int16_t *outX, int16_t *outY) {
  if (!fb_) return;
  Arduino_GFX *g = fb_;

  g->fillScreen(kScope);
  drawSweep_(sweepDeg);  // under the grid so rings stay crisp on top
  drawGrid_();

  int km = snap.rangeRingKm > 0 ? snap.rangeRingKm : 100;

  for (int i = 1; i <= 5; i++) {  // range labels up the north spoke
    char lbl[8];
    snprintf(lbl, sizeof(lbl), "%d", km * i / 5);
    text(g, cx_ + 6, cy_ - (rMax_ * i / 5) - 6, lbl, fontS(), kGDim, kLeft);
  }

  for (uint8_t i = 0; i < snap.count; i++) {
    const Aircraft &a = snap.ac[i];
    outX[i] = -1;
    outY[i] = -1;
    if (a.distanceKm > km) continue;

    float rr = (a.distanceKm / (float)km) * rMax_;
    float ang = (a.bearingDeg - 90.0f) * DEG_TO_RAD;
    int16_t x = cx_ + (int16_t)(cosf(ang) * rr);
    int16_t y = cy_ + (int16_t)(sinf(ang) * rr);
    outX[i] = x;  // in range => tappable regardless of sweep phase
    outY[i] = y;

    float ageAngle = fmodf(sweepDeg - a.bearingDeg + 360.0f, 360.0f);
    if (ageAngle > 260.0f && i != selectedIdx) continue;  // hidden between passes
    float fade = 1.0f;
    if (ageAngle > 200.0f) {
      fade = 1.0f - (ageAngle - 200.0f) / 60.0f;
      if (fade < 0.20f) fade = 0.20f;
    }
    if (i == selectedIdx && fade < 0.6f) fade = 0.6f;

    uint16_t col = blipColor_(a, fade);
    if (a.haveTrack) {
      float tr = (a.trackDeg - 90.0f) * DEG_TO_RAD;
      float fx = cosf(tr), fy = sinf(tr);
      float px = -fy, py = fx;
      float bx = x - fx * 4.0f, by = y - fy * 4.0f;  // back-center
      g->fillTriangle((int16_t)(x + fx * 7.0f), (int16_t)(y + fy * 7.0f),
                      (int16_t)(bx + px * 5.0f), (int16_t)(by + py * 5.0f),
                      (int16_t)(bx - px * 5.0f), (int16_t)(by - py * 5.0f), col);
    } else {
      g->fillCircle(x, y, 4, col);
    }
    g->drawCircle(x, y, 9, kGDim);
    if (i == selectedIdx) {
      g->drawCircle(x, y, 12, kGBright);
      g->drawCircle(x, y, 13, kGBright);
    }
    if ((ageAngle < 35.0f || i == selectedIdx) && a.callsign[0] != '\0') {
      text(g, x + 11, y - 14, a.callsign, fontS(), kGBright, kLeft);
    }
  }

  g->fillCircle(cx_, cy_, 3, kGBright);  // home
  g->drawCircle(cx_, cy_, 6, kGDim);
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
