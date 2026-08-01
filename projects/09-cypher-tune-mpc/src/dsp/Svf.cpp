#include "Svf.h"

#include <math.h>

namespace Dsp {
namespace {

// g = tan(pi*fc/fs) in Q16, and k = 1/Q in Q16.
//
// Q16 rather than Q12 for g: at 20 Hz g is 0.00196, which Q12 resolves to 8
// counts - a 12% cutoff error at the bottom of the sweep, audible as a filter
// that will not open smoothly. Q16 puts that under 0.2%.
//
// 2 KB of tables. They are plain statics rather than DRAM_ATTR because they are
// read at control rate (when a knob moves), not per sample, and the render path
// is flash-resident anyway - see the note in AudioEngine.cpp. If the render
// chain is ever moved to IRAM these should move with it.
uint32_t gTanG[256];
uint32_t gDampK[256];
bool gReady = false;

}  // namespace

float svfCutoffHz(uint8_t cutoffIdx, uint32_t engineRate) {
  const float lo = kSvfMinHz;
  const float hi = (float)engineRate * kSvfMaxNyquistFraction;
  return lo * powf(hi / lo, (float)cutoffIdx / 255.0f);
}

float svfResonanceQ(uint8_t resIdx) {
  return kSvfMinQ * powf(kSvfMaxQ / kSvfMinQ, (float)resIdx / 255.0f);
}

void initSvfTables(uint32_t engineRate) {
  if (engineRate == 0) {
    return;
  }
  for (int i = 0; i < 256; i++) {
    float fc = svfCutoffHz((uint8_t)i, engineRate);
    float g = tanf((float)M_PI * fc / (float)engineRate);
    if (g < 0.0f) {
      g = 0.0f;  // tan past the pole; the table top is capped to prevent this
    }
    gTanG[i] = (uint32_t)(g * 65536.0f + 0.5f);
    gDampK[i] = (uint32_t)(65536.0f / svfResonanceQ((uint8_t)i) + 0.5f);
  }
  gReady = true;
}

void Svf::setCutoff(uint8_t cutoffIdx, uint8_t resIdx) {
  if (!gReady) {
    // Never silently filter with garbage coefficients: pass audio through.
    a1 = 65536;
    a2 = 0;
    a3 = 0;
    k = 131072;
    return;
  }
  const int64_t g = (int64_t)gTanG[cutoffIdx];
  const int64_t kk = (int64_t)gDampK[resIdx];

  // a1 = 1/(1 + g*(g+k)), a2 = g*a1, a3 = g*a2 - all Q16, all in [0, 65536].
  const int64_t den = (int64_t)65536 + ((g * (g + kk)) >> 16);
  a1 = (int32_t)(((int64_t)1 << 32) / den);
  a2 = (int32_t)((g * a1) >> 16);
  a3 = (int32_t)((g * a2) >> 16);
  k = (int32_t)kk;
}

}  // namespace Dsp
