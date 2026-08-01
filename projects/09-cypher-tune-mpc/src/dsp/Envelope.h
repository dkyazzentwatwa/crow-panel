#ifndef CYPHER_TUNE_DSP_ENVELOPE_H
#define CYPHER_TUNE_DSP_ENVELOPE_H

#include <stdint.h>

// One-pole ADSR. Free of Arduino.h so the host tests compile the shipping code.
//
// This REPLACES two mechanisms that used to sit side by side in AudioEngine:
// the 16-frame linear attack ramp that de-clicked a voice start, and the
// 64-frame linear fade used for choke, steal and stop. Having both an envelope
// and a pair of ad-hoc ramps would mean two code paths that can disagree about
// a voice's current gain, so the ramps become envelope stages instead:
//
//   kAttackFrames = 16  ->  a minimum attack coefficient, enforced at gateOn()
//   kFadeFrames   = 64  ->  the kEnvKill stage's time constant
//
// A voice killed for buffer-ownership reasons (kit swap) still gets a hard
// stop, not a fade - the loop context is about to free() that PCM and a fade
// would keep reading it.
//
// WHY Q24 INTERNALLY, NOT Q15. The state is deliberately higher precision than
// the Q15 gain the mixer wants. A one-pole coefficient is 1 - exp(-1/tau), and
// for musically ordinary times that number is tiny: a 2.5 s attack at 32 kHz is
// 3.75e-5, which in Q15 rounds to 1, and an 8 s attack rounds to 0. With a Q15
// coefficient the update `(target - level) * coef >> 15` returns zero as soon as
// the gap falls under 32768, so the envelope silently stalls partway and the
// voice never reaches sustain or never frees - on hardware that presents as
// polyphony slowly dying. Q24 keeps ~200 counts of resolution even at the
// slowest setting. Call levelQ15() for the audio multiply.
//
// SHAPE. Attack targets 1.2x full scale so a one-pole reaches 1.0 in finite
// time and is then clamped; release/kill undershoot for the same reason. Decay
// and release are one-pole toward a target, which is the shape ears expect.
//
// RATE. step() is per sample. tick() advances the stage machine and is meant to
// run once per control block, not per sample.
namespace Dsp {

enum EnvStage : uint8_t {
  kEnvIdle = 0,
  kEnvAttack,
  kEnvDecay,
  kEnvSustain,
  kEnvRelease,
  kEnvKill,
};

static const int32_t kEnvOne = 1 << 24;              // Q24 full scale
static const int32_t kEnvAttackTarget = 20132659;    // 1.2x, so attack ends
static const int32_t kEnvFloorTarget = -2097152;     // -0.125x, so release ends
static const int32_t kEnvDoneLevel = 16384;          // ~0.001; voice is finished

// Legacy equivalence, kept as named constants so the intent survives:
// tau ~5 frames reaches full in ~16, tau ~20 reaches the floor in ~64.
static const int32_t kEnvMinAttackCoef = 3043328;    // Q24, ~5 frame tau
static const int32_t kEnvKillCoef = 818912;          // Q24, ~20 frame tau

// Maps an 0-255 index to a one-pole coefficient, ~1 ms to ~8 s. Built once from
// loop context; uses expf, which is flash-resident libm.
void initEnvTables(uint32_t engineRate);
float envTimeMs(uint8_t idx);

struct Env {
  int32_t level = 0;    // Q24
  int32_t target = 0;   // Q24
  int32_t coef = 0;     // Q24 one-pole coefficient
  uint8_t stage = kEnvIdle;
  uint8_t a = 0, d = 0, s = 255, r = 0;

  // Per sample. One 64-bit multiply, two adds.
  inline void step() {
    level += (int32_t)(((int64_t)(target - level) * coef) >> 24);
    if (level > kEnvOne) level = kEnvOne;
    if (level < 0) level = 0;
  }

  // Gain for the audio multiply.
  inline int32_t levelQ15() const { return level >> 9; }
  inline bool active() const { return stage != kEnvIdle; }

  void gateOn(uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release);
  void gateOff();
  // Fast, non-musical stop for choke, steal and transport stop. Always
  // terminates within ~96 frames regardless of the patch's release time.
  void kill();
  // Advances the stage machine. Call once per control block.
  void tick();
};

}  // namespace Dsp

#endif
