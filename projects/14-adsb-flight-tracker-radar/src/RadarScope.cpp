#include "RadarScope.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <CrowPanelShared.h>  // Widgets:: + fonts
#include <math.h>
#include <stdio.h>
#include "AdsbFormat.h"
#include "esp_heap_caps.h"

using namespace Widgets;

namespace {
// Radar green ramp + near-black scope background.
const uint16_t kScope = rgb(5, 12, 18);
const uint16_t kGDark = rgb(0, 46, 22);
const uint16_t kGDim = rgb(0, 92, 40);
const uint16_t kGMed = rgb(0, 168, 60);
const uint16_t kGBright = rgb(0, 255, 70);

// Altitude bands, ordered high -> low so the legend reads top-down. These are
// palette colours (not the old saturated scope-only greens) precisely so the
// same uint16_t can be used for the blip, the list dot and the legend swatch.
const uint16_t kSky = rgb(0x36, 0xB6, 0xFF);
const AltBand kBands[] = {
    {"30K+", kSky},        // >= 30000 ft
    {"10-30K", kGreen},    // >= 10000 ft
    {"<10K", kAmber},      // below 10000 ft
    {"GND", kTextMut},     // on the ground
    {"N/A", kRed},         // altitude unknown
};
constexpr uint8_t kBandCount = sizeof(kBands) / sizeof(kBands[0]);

// Scope geometry. rMax_ is inset far enough to leave a real bezel band rather
// than the single hairline the disc used to have.
constexpr int16_t kDiscInset = 30;   // canvas edge -> outer bezel
constexpr int16_t kBezelBand = 20;   // rMax_ -> outer bezel ring
constexpr int16_t kSweepTrailDeg = 60;
constexpr float kSweepTrailStep = 1.5f;
constexpr uint8_t kRingCount = 5;
constexpr float kBlipHideAngle = 260.0f;  // past this the blip is between passes
constexpr float kBlipFadeAngle = 200.0f;
constexpr float kBlipFadeSpan = 60.0f;
constexpr float kBlipFadeMin = 0.20f;
constexpr float kSelectedFadeMin = 0.6f;
constexpr float kLabelAngle = 35.0f;  // blips label only just after the sweep passes
constexpr uint8_t kRingedContacts = 3;  // only the nearest few get an acquisition ring

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

// One "LABEL  value" row inside the detail card.
void cardRow(Arduino_GFX *g, int16_t x, int16_t y, const char *label, const char *value,
             uint16_t valueColor) {
  text(g, x, y + 5, label, fontS(), kTextMut, kLeft);
  text(g, x + 52, y, value, fontL(), valueColor, kLeft);
}
}  // namespace

uint8_t RadarScope::altBandCount() { return kBandCount; }

const AltBand &RadarScope::altBandAt(uint8_t i) { return kBands[i < kBandCount ? i : 0]; }

const AltBand &RadarScope::altBand(const Aircraft &a) {
  if (a.onGround) return kBands[3];
  if (!a.haveAlt) return kBands[4];
  if (a.altFt >= 30000) return kBands[0];
  if (a.altFt >= 10000) return kBands[1];
  return kBands[2];
}

// Scale an RGB565 colour toward black without unpacking to 8-bit first.
uint16_t RadarScope::fadeColor(uint16_t c, float f) {
  if (f >= 1.0f) return c;
  if (f <= 0.0f) return 0;
  uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return (uint16_t)(((uint16_t)(r * f) << 11) | ((uint16_t)(g * f) << 5) | (uint16_t)(b * f));
}

void RadarScope::arrow(Arduino_GFX *g, int16_t cx, int16_t cy, float deg, float r,
                       uint16_t color) {
  float a = (deg - 90.0f) * DEG_TO_RAD;
  float fx = cosf(a), fy = sinf(a);
  float px = -fy, py = fx;                       // perpendicular, for the base corners
  float bx = cx - fx * r * 0.7f, by = cy - fy * r * 0.7f;
  g->fillTriangle((int16_t)(cx + fx * r), (int16_t)(cy + fy * r),
                  (int16_t)(bx + px * r * 0.55f), (int16_t)(by + py * r * 0.55f),
                  (int16_t)(bx - px * r * 0.55f), (int16_t)(by - py * r * 0.55f), color);
}

String RadarScope::fit(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String t = s;
  while (t.length() > 1 && textWidth(g, (t + "...").c_str(), font) > maxW) {
    t.remove(t.length() - 1);
  }
  return t + "...";
}

bool RadarScope::begin(int16_t w, int16_t h) {
  w_ = w & ~1;  // even width => the uint32 fast-copy path in the blit engages
  h_ = h;
  cx_ = w_ / 2;
  cy_ = h_ / 2;
  rMax_ = (cx_ < cy_ ? cx_ : cy_) - kDiscInset;

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

void RadarScope::drawGrid_() {
  Arduino_GFX *g = fb_;
  for (int i = 1; i < kRingCount; i++) {
    g->drawCircle(cx_, cy_, rMax_ * i / kRingCount, kGDark);
  }

  for (int a = 0; a < 360; a += 30) {  // spokes
    float rad = (a - 90) * DEG_TO_RAD;
    g->drawLine(cx_, cy_, cx_ + (int)(cosf(rad) * rMax_), cy_ + (int)(sinf(rad) * rMax_), kGDark);
  }
  g->drawLine(cx_, cy_ - rMax_, cx_, cy_ + rMax_, kGDim);  // N-S
  g->drawLine(cx_ - rMax_, cy_, cx_ + rMax_, cy_, kGDim);  // E-W

  // Cardinals sit INSIDE the bezel band so the tick ring stays uncluttered.
  const int16_t rc = rMax_ - 14;
  text(g, cx_, cy_ - rc - 8, "N", fontS(), kGBright, kCenter);
  text(g, cx_, cy_ + rc - 8, "S", fontS(), kGDim, kCenter);
  text(g, cx_ + rc - 4, cy_ - 7, "E", fontS(), kGDim, kRight);
  text(g, cx_ - rc + 4, cy_ - 7, "W", fontS(), kGDim, kLeft);
}

// Compass bezel: a graduated ring with 10/30/90-degree ticks and a solid north
// index wedge. Pure geometry (~2.5k pixels, no glyphs), so it costs almost
// nothing in the per-frame recompose. Numeric bearing labels are deliberately
// NOT drawn - twelve 3-char labels would be 36 glyph decodes every frame for
// information the graduations already carry.
void RadarScope::drawBezel_() {
  Arduino_GFX *g = fb_;
  const int16_t rIn = rMax_ + 2;
  const int16_t rOut = rMax_ + kBezelBand;
  g->drawCircle(cx_, cy_, rMax_, kGMed);  // the disc edge itself
  g->drawCircle(cx_, cy_, rIn, kGDim);
  g->drawCircle(cx_, cy_, rOut, kGMed);

  for (int a = 0; a < 360; a += 10) {
    bool cardinal = (a % 90 == 0);
    bool major = (a % 30 == 0);
    int16_t r0 = cardinal ? rIn : (major ? rOut - 12 : rOut - 6);
    uint16_t col = cardinal ? kGMed : (major ? kGDim : kGDark);
    float rad = (a - 90) * DEG_TO_RAD;
    float ca = cosf(rad), sa = sinf(rad);
    g->drawLine(cx_ + (int16_t)(ca * r0), cy_ + (int16_t)(sa * r0),
                cx_ + (int16_t)(ca * rOut), cy_ + (int16_t)(sa * rOut), col);
  }

  // Lubber line: north index wedge, apex inward.
  g->fillTriangle(cx_, cy_ - rIn, cx_ - 7, cy_ - rOut, cx_ + 7, cy_ - rOut, kGBright);
}

void RadarScope::drawSweep_(float sweepDeg) {
  Arduino_GFX *g = fb_;
  for (float i = kSweepTrailDeg; i >= 0.0f; i -= kSweepTrailStep) {
    float a = sweepDeg - i;
    float intensity = (kSweepTrailDeg - i) / kSweepTrailDeg;  // 0 at the tail, 1 at the edge
    uint16_t col = rgb(0, (uint8_t)(6 + intensity * 120), 0);
    float rad = (a - 90.0f) * DEG_TO_RAD;
    g->drawLine(cx_, cy_, cx_ + (int16_t)(cosf(rad) * rMax_), cy_ + (int16_t)(sinf(rad) * rMax_),
                col);
  }
  float rad = (sweepDeg - 90.0f) * DEG_TO_RAD;  // bright leading edge
  g->drawLine(cx_, cy_, cx_ + (int16_t)(cosf(rad) * rMax_), cy_ + (int16_t)(sinf(rad) * rMax_),
              kGBright);
  // Head dot riding the bezel band - reads as a rotating instrument index.
  g->fillCircle(cx_ + (int16_t)(cosf(rad) * (rMax_ + kBezelBand / 2)),
                cy_ + (int16_t)(sinf(rad) * (rMax_ + kBezelBand / 2)), 4, kGBright);
}

// Range labels on the SE diagonal rather than up the north spoke, where the
// sweep and the lubber wedge both used to cross them. Each gets a backing
// punch-out so the grid doesn't show through the glyphs.
void RadarScope::drawRangeLabels_(int km) {
  Arduino_GFX *g = fb_;
  static const uint8_t kLabelRings[] = {2, 4, 5};
  for (uint8_t k = 0; k < sizeof(kLabelRings) / sizeof(kLabelRings[0]); k++) {
    uint8_t i = kLabelRings[k];
    int16_t rr = rMax_ * i / kRingCount;
    int16_t lx = cx_ + (int16_t)(0.7071f * rr) + 4;
    int16_t ly = cy_ + (int16_t)(0.7071f * rr) - 6;
    char lbl[10];
    if (i == kRingCount) {
      snprintf(lbl, sizeof(lbl), "%dkm", km);
    } else {
      snprintf(lbl, sizeof(lbl), "%d", km * i / kRingCount);
    }
    g->fillRect(lx - 2, ly - 1, textWidth(g, lbl, fontS()) + 4, 14, kScope);
    text(g, lx, ly, lbl, fontS(), kGDim, kLeft);
  }
}

void RadarScope::render(const AdsbSnapshot &snap, float sweepDeg, int8_t selectedIdx,
                        int16_t *outX, int16_t *outY, bool cardOpen) {
  if (!fb_) return;
  Arduino_GFX *g = fb_;

  // When the card is open it covers its own rect opaquely, so clearing that
  // area here is work thrown away twice a frame. The strips beside the card
  // still have to be cleared - the disc and sweep run behind them.
  if (cardOpen) {
    g->fillRect(0, 0, w_, kCardY, kScope);
    g->fillRect(0, kCardY + kCardH, w_, h_ - (kCardY + kCardH), kScope);
    g->fillRect(0, kCardY, kCardX, kCardH, kScope);
    g->fillRect(kCardX + kCardW, kCardY, w_ - (kCardX + kCardW), kCardH, kScope);
  } else {
    g->fillScreen(kScope);
  }

  drawSweep_(sweepDeg);  // under the grid so rings stay crisp on top
  drawGrid_();
  drawBezel_();

  int km = snap.rangeRingKm > 0 ? snap.rangeRingKm : 100;
  drawRangeLabels_(km);

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
    if (ageAngle > kBlipHideAngle && i != selectedIdx) continue;  // hidden between passes
    float fade = 1.0f;
    if (ageAngle > kBlipFadeAngle) {
      fade = 1.0f - (ageAngle - kBlipFadeAngle) / kBlipFadeSpan;
      if (fade < kBlipFadeMin) fade = kBlipFadeMin;
    }
    if (i == selectedIdx && fade < kSelectedFadeMin) fade = kSelectedFadeMin;

    uint16_t band = altBand(a).color;
    uint16_t col = fadeColor(band, fade);
    if (a.haveTrack) {
      // Velocity leader: length scales with ground speed, so the disc shows
      // where traffic is going, not just where it is.
      float tr = (a.trackDeg - 90.0f) * DEG_TO_RAD;
      float fx = cosf(tr), fy = sinf(tr);
      float len = 6.0f + fminf(a.groundSpeedKt / 12.0f, 16.0f);
      g->drawLine(x, y, (int16_t)(x + fx * len), (int16_t)(y + fy * len),
                  fadeColor(band, fade * 0.7f));
      arrow(g, x, y, a.trackDeg, 7.0f, col);
    } else {
      g->fillCircle(x, y, 4, col);
    }

    // Acquisition rings only on the nearest few: at 12+ contacts a ring on
    // every blip merges into visual mush.
    if (i < kRingedContacts && i != selectedIdx) g->drawCircle(x, y, 9, kGDim);

    if (i == selectedIdx) {
      // Corner-bracket reticle reads as "target locked" and is cheaper than the
      // pair of full circles it replaces.
      const int16_t r = 14, len = 6;
      for (int8_t sx = -1; sx <= 1; sx += 2) {
        for (int8_t sy = -1; sy <= 1; sy += 2) {
          g->drawFastHLine(x + (sx < 0 ? -r : r - len), y + sy * r, len, kGBright);
          g->drawFastVLine(x + sx * r, y + (sy < 0 ? -r : r - len), len, kGBright);
        }
      }
    }
    if ((ageAngle < kLabelAngle || i == selectedIdx) && a.callsign[0] != '\0') {
      int16_t lw = textWidth(g, a.callsign, fontS());
      g->fillRect(x + 9, y - 16, lw + 4, 14, kScope);  // keep labels legible over the grid
      text(g, x + 11, y - 14, a.callsign, fontS(), kGBright, kLeft);
    }
  }

  // Home: dot, ring and a small crosshair.
  g->fillCircle(cx_, cy_, 3, kGBright);
  g->drawCircle(cx_, cy_, 6, kGDim);
  g->drawFastHLine(cx_ - 10, cy_, 21, kGDim);
  g->drawFastVLine(cx_, cy_ - 10, 21, kGDim);
}

void RadarScope::renderDetail(const Aircraft &a) {
  if (!fb_) return;
  Arduino_GFX *g = fb_;
  const int16_t X = kCardX, Y = kCardY, W = kCardW, H = kCardH;
  const AltBand &b = altBand(a);

  // Border in the aircraft's own altitude band: the card, the blip and the list
  // dot are then unmistakably the same object.
  panel(g, X, Y, W, H, 12, kSurface, 2, b.color);

  // --- header: callsign + close affordance ---
  pill(g, X + W - 74, Y + 8, "CLOSE", fontS(), kBg, kAccent);
  statusDot(g, X + 24, Y + 24, 6, b.color);
  String cs = a.callsign[0] ? String(a.callsign) : String("UNKNOWN");
  text(g, X + 40, Y + 8, fit(g, cs, fontL(), W - 40 - 86).c_str(), fontL(), kTextHi, kLeft);

  text(g, X + 18, Y + 38, a.icao, fontS(), kTextMut, kLeft);
  char tc[28];
  fmtTypeCat(tc, sizeof(tc), a);
  if (tc[0]) {
    text(g, X + W - 18, Y + 38, fit(g, String(tc), fontS(), 160).c_str(), fontS(), kTextHi, kRight);
  }
  g->drawFastHLine(X + 16, Y + 58, W - 32, kLine);

  // --- left column: the four numbers, one per row ---
  char altS[16], trkS[8], spdS[16], rngS[16];
  fmtAlt(altS, sizeof(altS), a);
  if (a.haveTrack) {
    snprintf(trkS, sizeof(trkS), "%03d", bearing360(a.trackDeg));
  } else {
    snprintf(trkS, sizeof(trkS), "---");
  }
  snprintf(spdS, sizeof(spdS), "%.0f kt", a.groundSpeedKt);
  snprintf(rngS, sizeof(rngS), "%.1f km", a.distanceKm);

  const int16_t colX = X + 18;
  cardRow(g, colX, Y + 70, "ALT", altS, b.color);
  cardRow(g, colX, Y + 102, "TRK", trkS, kTextHi);
  cardRow(g, colX, Y + 134, "SPD", spdS, kTextHi);
  cardRow(g, colX, Y + 166, "RNG", rngS, kTextHi);

  // --- right: heading rosette ---
  const int16_t rcx = X + 262, rcy = Y + 116, rr = 44;
  g->drawCircle(rcx, rcy, rr, kLine);
  g->drawCircle(rcx, rcy, rr - 2, kLine);
  for (int d = 0; d < 360; d += 30) {
    float rad = (d - 90) * DEG_TO_RAD;
    float ca = cosf(rad), sa = sinf(rad);
    g->drawLine(rcx + (int16_t)(ca * (rr - 8)), rcy + (int16_t)(sa * (rr - 8)),
                rcx + (int16_t)(ca * (rr - 3)), rcy + (int16_t)(sa * (rr - 3)),
                (d % 90 == 0) ? kTextMut : kLine);
  }
  text(g, rcx, rcy - rr - 16, "TRACK", fontS(), kTextMut, kCenter);
  if (a.haveTrack) {
    arrow(g, rcx, rcy, a.trackDeg, 34.0f, b.color);
    g->fillCircle(rcx, rcy, 3, kTextHi);
  } else {
    text(g, rcx, rcy - 7, "N/A", fontS(), kTextMut, kCenter);
  }
  text(g, X + W - 18, Y + 176, "tap anywhere to close", fontS(), kTextMut, kRight);

  // --- altitude profile, 0..45000 ft ---
  text(g, X + 18, Y + 205, "ALT", fontS(), kTextMut, kLeft);
  float norm = (a.haveAlt && !a.onGround) ? (float)a.altFt / 45000.0f : 0.0f;
  hBar(g, X + 52, Y + 204, W - 52 - 34, 12, norm, b.color, kLine);
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
