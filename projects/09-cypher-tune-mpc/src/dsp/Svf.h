#ifndef CYPHER_TUNE_DSP_SVF_H
#define CYPHER_TUNE_DSP_SVF_H

#include <stdint.h>

// Topology-preserving (zero-delay-feedback) state-variable filter, after
// Zavalishin. Free of Arduino.h on purpose so scripts/test-cypher-tune.sh
// compiles the shipping code rather than a copy.
//
// WHY NOT THE CLASSIC CHAMBERLIN SVF. The usual `f = 2*sin(pi*fc/fs)` form is
// only unconditionally stable to roughly fs/6 - 5.3 kHz at 32 kHz. A groovebox
// needs to sweep a filter to the top of the band, and a filter that blows up at
// high cutoff and high resonance does not produce a polite artifact: it
// produces a full-scale burst into a small speaker amp. The TPT form is stable
// at every cutoff, gives all four responses from one state pair, and costs
// about five multiplies.
//
// KNOWN CHARACTERISTIC: at the top of the resonance range the output clamp
// engages. The host sweep drives both |out| and |state| to exactly their limits
// with full-scale noise at Q=12, which is a resonant filter behaving normally -
// a Q of 12 really is ~12x gain at the corner. The clamps are what turn that
// into clipping instead of int32 wraparound and a full-scale sign flip. Real
// program material is nowhere near full-scale white noise, so this may never be
// heard; if it is, the fix is to scale the input down as resonance rises rather
// than to loosen the clamps.
//
// NUMBER FORMAT. Coefficients are Q16 and provably land in [0, 65536]; state is
// Q23.8 (a sample scaled <<8), which keeps 8 fractional bits so low cutoffs do
// not quantise into a staircase. The per-sample multiplies must go through
// int64: at high resonance the state legitimately reaches several times full
// scale, and coeff * state overflows int32 well before that. This is the one
// place in the engine where the 64-bit intermediate is not optional.
namespace Dsp {

enum FilterType : uint8_t {
  kFilterOff = 0,
  kFilterLP,
  kFilterBP,
  kFilterHP,
  kFilterNotch,
  kFilterTypeCount,
};

// Full scale for a 16-bit sample in the Q23.8 state domain.
static const int32_t kSvfFullScale = 32767 << 8;      // ~2^23
// Output clamp: a resonant peak may be loud, but it must never wrap into a
// sign flip - that is the sound that destroys speakers.
static const int32_t kSvfOutMax = kSvfFullScale * 4;  // ~2^25
// State clamp: catches divergence and, just as importantly, guarantees the
// `2*v - ic` integrator update cannot overflow int32.
static const int32_t kSvfStateMax = 1 << 27;

// Cutoff index 0-255 maps log-wise over this range. The top is capped well
// below Nyquist: g = tan(pi*fc/fs) runs away at the pole.
static const float kSvfMinHz = 20.0f;
static const float kSvfMaxNyquistFraction = 0.45f;
// Resonance index 0-255 maps log-wise over this Q range. The ceiling stops
// short of true self-oscillation on purpose.
static const float kSvfMinQ = 0.5f;
static const float kSvfMaxQ = 12.0f;

// Builds the cutoff/resonance tables for a given engine rate. Call once from
// loop context before the render task starts - it uses tanf, which is
// flash-resident libm and has no business running on the audio task.
void initSvfTables(uint32_t engineRate);
// Exposed for tests and for anything that wants to label a knob.
float svfCutoffHz(uint8_t cutoffIdx, uint32_t engineRate);
float svfResonanceQ(uint8_t resIdx);

struct Svf {
  int32_t a1 = 65536, a2 = 0, a3 = 0;  // Q16
  int32_t k = 131072;                  // Q16, = 1/Q
  int32_t ic1 = 0, ic2 = 0;            // Q23.8 integrator state
  uint8_t type = kFilterOff;

  void reset() { ic1 = 0; ic2 = 0; }
  void setType(uint8_t t) { type = t < kFilterTypeCount ? t : kFilterOff; }

  // Control rate only. Reads the tables built by initSvfTables().
  void setCutoff(uint8_t cutoffIdx, uint8_t resIdx);

  // Per sample. `x` and the return are Q23.8.
  inline int32_t process(int32_t x) {
    if (type == kFilterOff) {
      return x;
    }
    const int32_t v3 = x - ic2;
    const int32_t v1 = mul(a1, ic1) + mul(a2, v3);
    const int32_t v2 = ic2 + mul(a2, ic1) + mul(a3, v3);

    // Clamp before doubling so 2*v cannot overflow.
    ic1 = clampState(2 * clampState(v1) - ic1);
    ic2 = clampState(2 * clampState(v2) - ic2);

    int32_t out;
    switch (type) {
      case kFilterLP:    out = v2; break;
      case kFilterBP:    out = v1; break;
      case kFilterHP:    out = x - mul(k, v1) - v2; break;
      case kFilterNotch: out = x - mul(k, v1); break;
      default:           out = x; break;
    }
    if (out > kSvfOutMax) return kSvfOutMax;
    if (out < -kSvfOutMax) return -kSvfOutMax;
    return out;
  }

 private:
  static inline int32_t mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
  }
  static inline int32_t clampState(int32_t v) {
    if (v > kSvfStateMax) return kSvfStateMax;
    if (v < -kSvfStateMax) return -kSvfStateMax;
    return v;
  }
};

}  // namespace Dsp

#endif
