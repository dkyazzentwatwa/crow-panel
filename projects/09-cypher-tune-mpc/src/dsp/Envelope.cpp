#include "Envelope.h"

#include <math.h>

namespace Dsp {
namespace {

// One-pole coefficient per 0-255 index, ~1 ms to ~8 s at the engine rate,
// coef = 1 - exp(-1/tauFrames) in Q24. See the header for why Q24 rather than
// the Q15 the mixer uses: at Q15 a 2.5 s attack rounds to 1 and an 8 s attack
// rounds to 0, and the envelope stalls instead of moving.
int32_t gCoef[256];
bool gReady = false;

const float kEnvMinMs = 1.0f;
const float kEnvMaxMs = 8000.0f;

}  // namespace

float envTimeMs(uint8_t idx) {
  return kEnvMinMs * powf(kEnvMaxMs / kEnvMinMs, (float)idx / 255.0f);
}

void initEnvTables(uint32_t engineRate) {
  if (engineRate == 0) {
    return;
  }
  for (int i = 0; i < 256; i++) {
    float ms = envTimeMs((uint8_t)i);
    // A one-pole covers ~63% per tau; aim tau at a third of the requested time
    // so three taus (~95%) is where the stage-exit test fires.
    float tauFrames = (ms * 0.001f * (float)engineRate) / 3.0f;
    if (tauFrames < 1.0f) {
      tauFrames = 1.0f;
    }
    float c = 1.0f - expf(-1.0f / tauFrames);
    int32_t q = (int32_t)(c * (float)kEnvOne + 0.5f);
    // Never zero: a zero coefficient is a permanently frozen envelope, which
    // leaks the voice rather than merely sounding wrong.
    if (q < 1) q = 1;
    if (q > kEnvOne) q = kEnvOne;
    gCoef[i] = q;
  }
  gReady = true;
}

static int32_t coefFor(uint8_t idx) {
  return gReady ? gCoef[idx] : (kEnvOne / 2);  // fallback: fast but sane
}

void Env::gateOn(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release) {
  a = attack;
  d = decay;
  s = sustain;
  r = release;
  stage = kEnvAttack;
  target = kEnvAttackTarget;
  coef = coefFor(a);
  // Enforce the de-click minimum. A patch asking for a 0 ms attack still gets
  // ~16 frames of ramp - what the old kAttackFrames constant did, and the
  // difference between a transient and a click.
  if (coef > kEnvMinAttackCoef) {
    coef = kEnvMinAttackCoef;
  }
  // Deliberately does NOT reset level: retriggering a still-sounding voice
  // ramps from where it is rather than jumping to zero and clicking.
}

void Env::gateOff() {
  if (stage == kEnvIdle || stage == kEnvKill) {
    return;
  }
  stage = kEnvRelease;
  target = kEnvFloorTarget;
  coef = coefFor(r);
}

void Env::kill() {
  stage = kEnvKill;
  target = kEnvFloorTarget;
  coef = kEnvKillCoef;
}

void Env::tick() {
  switch (stage) {
    case kEnvAttack:
      if (level >= kEnvOne - 1) {
        stage = kEnvDecay;
        target = (int32_t)(((int64_t)s * kEnvOne) / 255);
        coef = coefFor(d);
      }
      break;
    case kEnvDecay: {
      const int32_t sustainLevel = (int32_t)(((int64_t)s * kEnvOne) / 255);
      const int32_t gap = level - sustainLevel;
      if (gap < kEnvDoneLevel && gap > -kEnvDoneLevel) {
        stage = kEnvSustain;
        level = sustainLevel;
        target = sustainLevel;
        coef = 0;  // hold
      }
      break;
    }
    case kEnvSustain:
      break;
    case kEnvRelease:
    case kEnvKill:
      if (level <= kEnvDoneLevel) {
        level = 0;
        target = 0;
        coef = 0;
        stage = kEnvIdle;
      }
      break;
    default:
      break;
  }
}

}  // namespace Dsp
