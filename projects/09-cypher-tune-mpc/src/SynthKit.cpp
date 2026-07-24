#include "SynthKit.h"

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
int16_t *renderPad(uint8_t pad, uint32_t rate, uint32_t *framesOut) {
  float duration;
  switch (pad) {
    case 0: duration = 0.35f; break;   // Kick
    case 1: duration = 0.28f; break;   // Snare
    case 2: duration = 0.09f; break;   // Hat
    case 3: duration = 0.45f; break;   // OpenHat
    case 4: duration = 0.30f; break;   // Clap
    case 5: duration = 0.12f; break;   // Rim
    case 6: duration = 0.20f; break;   // PercA
    case 7: duration = 0.15f; break;   // PercB
    case 8: duration = 0.45f; break;   // BassA
    case 9: duration = 0.45f; break;   // BassB
    case 10: duration = 0.50f; break;  // ChordA
    case 11: duration = 0.50f; break;  // ChordB
    case 12: duration = 0.35f; break;  // VoxA
    case 13: duration = 0.35f; break;  // VoxB
    default: duration = 0.50f; break;  // FxUp / FxDn
  }

  uint32_t frames = (uint32_t)(duration * rate);
  if (frames == 0) {
    return nullptr;
  }
  int16_t *pcm = SampleBank::allocFrames(frames);
  if (pcm == nullptr) {
    return nullptr;
  }

  noiseState = 0x1234ABCD + pad * 0x9E3779B9u;  // fresh, per-pad stream
  HighPass hatHp(0.9f);
  HighPass clapHp(0.8f);
  float phase = 0.0f;
  float phase2 = 0.0f;

  for (uint32_t n = 0; n < frames; n++) {
    float t = (float)n / rate;
    float value = 0.0f;

    switch (pad) {
      case 0: {  // Kick: exponential 120->45 Hz sweep + 3 ms click
        float freq = 45.0f + 75.0f * expf(-t / 0.06f);
        phase += kTwoPi * freq / rate;
        value = sinf(phase) * expf(-t / 0.13f);
        if (t < 0.003f) {
          value += 0.5f * noise() * expf(-t / 0.002f);
        }
        break;
      }
      case 1: {  // Snare: 190 Hz body + bright noise
        phase += kTwoPi * 190.0f / rate;
        value = 0.5f * sinf(phase) * expf(-t / 0.055f) +
                0.6f * hatHp.process(noise()) * expf(-t / 0.09f);
        break;
      }
      case 2:  // Hat: short filtered noise
        value = hatHp.process(noise()) * expf(-t / 0.025f);
        break;
      case 3:  // OpenHat: same voice, long tail (choke group cuts it)
        value = hatHp.process(noise()) * expf(-t / 0.15f);
        break;
      case 4: {  // Clap: three 12 ms-spaced bursts + tail
        float env = 0.0f;
        for (uint8_t burst = 0; burst < 3; burst++) {
          float tb = t - 0.012f * burst;
          if (tb >= 0.0f) {
            env += expf(-tb / 0.008f);
          }
        }
        if (t > 0.03f) {
          env += 0.7f * expf(-(t - 0.03f) / 0.07f);
        }
        value = clapHp.process(noise()) * env * 0.8f;
        break;
      }
      case 5: {  // Rim: damped 800 Hz ping + tick
        phase += kTwoPi * 800.0f / rate;
        value = sinf(phase) * expf(-t / 0.015f);
        if (t < 0.002f) {
          value += 0.4f * noise();
        }
        break;
      }
      case 6:    // PercA/PercB: FM woodblock
      case 7: {
        float base = (pad == 6) ? 220.0f : 330.0f;
        float tau = (pad == 6) ? 0.06f : 0.05f;
        phase += kTwoPi * base / rate;
        phase2 += kTwoPi * base * 2.0f / rate;
        value = sinf(phase + 0.3f * sinf(phase2)) * expf(-t / tau);
        break;
      }
      case 8:    // BassA/BassB: harmonic-rich decaying note (A1 / D2)
      case 9: {
        float base = (pad == 8) ? 55.0f : 73.42f;
        phase += kTwoPi * base / rate;
        value = (sinf(phase) + 0.35f * sinf(2.0f * phase) + 0.2f * sinf(3.0f * phase)) *
                0.65f * expf(-t / 0.22f);
        break;
      }
      case 10:   // ChordA/ChordB: detuned triad, soft attack
      case 11: {
        const float *freqs;
        static const float major[3] = {261.63f, 329.63f, 392.0f};
        static const float minor[3] = {220.0f, 261.63f, 329.63f};
        freqs = (pad == 10) ? major : minor;
        float sum = 0.0f;
        for (uint8_t v = 0; v < 3; v++) {
          sum += sinf(kTwoPi * freqs[v] * (1.0f + 0.002f * v) * t);
        }
        float attack = t < 0.005f ? t / 0.005f : 1.0f;
        value = sum / 3.0f * attack * expf(-t / 0.28f);
        break;
      }
      case 12:   // VoxA/VoxB: vibrato tone + breath
      case 13: {
        float base = (pad == 12) ? 440.0f : 330.0f;
        float vibrato = 0.15f * sinf(kTwoPi * 5.5f * t);
        phase += kTwoPi * base * (1.0f + vibrato * 0.01f) / rate;
        value = sinf(phase + vibrato) * expf(-t / 0.16f) +
                0.1f * hatHp.process(noise()) * expf(-t / 0.05f);
        break;
      }
      default: {  // FxUp / FxDn: exponential sweep
        bool up = (pad == 14);
        float from = up ? 200.0f : 1400.0f;
        float to = up ? 1400.0f : 200.0f;
        float freq = from * powf(to / from, t / duration);
        phase += kTwoPi * freq / rate;
        float attack = t < 0.01f ? t / 0.01f : 1.0f;
        value = sinf(phase) * attack * expf(-t / 0.25f);
        break;
      }
    }

    pcm[n] = (int16_t)(clamp1(value) * kPeak);
  }

  *framesOut = frames;
  return pcm;
}

const char *builtinRef(uint8_t pad) {
  static const char *kRefs[SampleBank::kPadCount] = {
      "builtin:kick", "builtin:snare", "builtin:hat", "builtin:openhat",
      "builtin:clap", "builtin:rim", "builtin:perc-a", "builtin:perc-b",
      "builtin:bass-a", "builtin:bass-b", "builtin:chord-a", "builtin:chord-b",
      "builtin:vox-a", "builtin:vox-b", "builtin:fx-up", "builtin:fx-down"};
  return kRefs[pad < SampleBank::kPadCount ? pad : 0];
}

}  // namespace

namespace SynthKit {

bool synthesizePad(SampleBank &bank, uint8_t pad) {
  if (pad >= SampleBank::kPadCount) {
    return false;
  }
  uint32_t frames = 0;
  int16_t *pcm = renderPad(pad, bank.engineRate(), &frames);
  if (pcm == nullptr) {
    return false;
  }
  if (!bank.adoptPcm(pad, pcm, frames, bank.engineRate(), builtinRef(pad))) {
    free(pcm);
    return false;
  }
  return true;
}

uint8_t synthesizeBuiltinKit(SampleBank &bank) {
  uint8_t loaded = 0;
  for (uint8_t pad = 0; pad < SampleBank::kPadCount; pad++) {
    if (synthesizePad(bank, pad)) {
      loaded++;
    }
  }
  bank.setKitName("builtin");
  return loaded;
}

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
