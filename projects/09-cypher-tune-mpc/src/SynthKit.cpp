#include "SynthKit.h"

// Only the metronome is synthesized now: the playable kit is real drum
// recordings baked into flash (KitSamples.cpp / BuiltinKit).

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kPeak = 27500.0f;  // ~0.84 FS: headroom for velocity accents

uint32_t noiseState = 0x1234ABCD;

// xorshift32, mapped to [-1, 1]. Deterministic so the kit sounds identical
// every boot.
float noise() {
  noiseState ^= noiseState << 13;
  noiseState ^= noiseState >> 17;
  noiseState ^= noiseState << 5;
  return (int32_t)noiseState / 2147483648.0f;
}

// One-pole highpass; keeps hats/claps from thumping.
struct HighPass {
  float alpha;
  float lastIn = 0.0f;
  float lastOut = 0.0f;
  explicit HighPass(float a) : alpha(a) {}
  float process(float in) {
    lastOut = alpha * (lastOut + in - lastIn);
    lastIn = in;
    return lastOut;
  }
};

float clamp1(float v) {
  if (v > 1.0f) return 1.0f;
  if (v < -1.0f) return -1.0f;
  return v;
}

// Every generator writes value(t) in [-1, 1] for frame n of `frames`.
// The buffer conversion is shared.


}  // namespace

namespace SynthKit {



int16_t *synthesizeMetronome(bool accent, uint32_t engineRate, uint32_t *framesOut) {
  float duration = accent ? 0.03f : 0.025f;
  float freq = accent ? 1600.0f : 1000.0f;
  float tau = accent ? 0.006f : 0.005f;
  uint32_t frames = (uint32_t)(duration * engineRate);
  int16_t *pcm = SampleBank::allocFrames(frames);
  if (pcm == nullptr) {
    return nullptr;
  }
  float phase = 0.0f;
  for (uint32_t n = 0; n < frames; n++) {
    float t = (float)n / engineRate;
    phase += kTwoPi * freq / engineRate;
    pcm[n] = (int16_t)(clamp1(sinf(phase) * expf(-t / tau)) * kPeak * 0.7f);
  }
  *framesOut = frames;
  return pcm;
}

}  // namespace SynthKit
