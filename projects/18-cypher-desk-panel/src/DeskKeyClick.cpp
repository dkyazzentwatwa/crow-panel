#include "DeskKeyClick.h"

namespace {
// Named in every build so a silent build can still report what it would play.
// Order must match DeskKeyClick::Sound.
const char *const kNames[] = {"Off", "Pencil", "Typewriter", "Mechanical"};
}  // namespace

const char *DeskKeyClick::soundName(uint8_t sound) {
  return kNames[sound < kSoundCount ? sound : 0];
}
const char *DeskKeyClick::soundName() const { return soundName(sound_); }
uint8_t DeskKeyClick::sound() const { return sound_; }
void DeskKeyClick::setSound(uint8_t sound) { sound_ = sound < kSoundCount ? sound : 0; }
bool DeskKeyClick::ready() const { return ready_; }

#if CYPHER_DESK_AUDIO_BACKEND

#include <esp_heap_caps.h>
#include <math.h>

namespace {

constexpr uint32_t kRate = DeskAudioEngine::kOutputRate;
constexpr float kTwoPi = 6.28318530718f;
// ~0.61 FS per clip peak. Voices are clamped rather than scaled, so this is the
// headroom that keeps two overlapping clicks clean over music.
constexpr float kPeak = 20000.0f;

// One typing sound: three exponentially decaying layers summed.
//   noise - the paper/plastic transient. One-pole lowpassed; lpAlpha is the
//           brightness knob (0.85 ~ raw hiss, 0.12 ~ a muffled bump).
//   tone  - the click's resonant pitch.
//   body  - the low "bottoming out" thud.
struct ClickSpec {
  float durMs;
  float toneHz, toneTauMs, toneLevel;
  float noiseLevel, noiseTauMs, lpAlpha;
  float bodyHz, bodyTauMs, bodyLevel;
  float peak;  // overall level 0..1, relative to kPeak
};

// [sound - 1][0 = press, 1 = release]
const ClickSpec kSpecs[3][2] = {
    // Pencil: almost all soft noise, very short, barely any pitch. The quiet
    // default for a writing deck - present enough to feel, easy to ignore.
    {{16.0f, 900.0f, 2.0f, 0.10f, 0.80f, 1.6f, 0.45f, 130.0f, 6.0f, 0.10f, 0.34f},
     {10.0f, 700.0f, 1.6f, 0.05f, 0.40f, 1.0f, 0.40f, 110.0f, 4.0f, 0.05f, 0.16f}},
    // Typewriter: a hard bright strike with a real mechanical body, and an
    // audible return on the way up.
    {{30.0f, 3100.0f, 2.6f, 0.55f, 0.70f, 1.4f, 0.78f, 165.0f, 14.0f, 0.60f, 0.95f},
     {20.0f, 2200.0f, 2.2f, 0.40f, 0.45f, 1.1f, 0.70f, 200.0f, 7.0f, 0.30f, 0.62f}},
    // Mechanical: the tactile keyboard click - lower and more damped than the
    // typewriter, with a clean second click on release.
    {{24.0f, 2600.0f, 3.5f, 0.72f, 0.55f, 1.1f, 0.62f, 190.0f, 9.0f, 0.32f, 0.72f},
     {18.0f, 1900.0f, 3.0f, 0.70f, 0.40f, 1.0f, 0.58f, 220.0f, 5.0f, 0.14f, 0.52f}},
};

// Per-variant jitter. Deterministic, so the bank is identical every boot.
const float kPitchJitter[DeskKeyClick::kVariants] = {0.94f, 1.00f, 1.07f};
const float kLevelJitter[DeskKeyClick::kVariants] = {0.90f, 1.00f, 0.95f};
const float kTauJitter[DeskKeyClick::kVariants] = {0.93f, 1.00f, 1.08f};

float clampUnit(float value) {
  if (value > 1.0f) return 1.0f;
  if (value < -1.0f) return -1.0f;
  return value;
}

int16_t *allocatePcm(uint32_t frames) {
  const size_t bytes = frames * sizeof(int16_t);
  int16_t *pcm = static_cast<int16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  if (pcm == nullptr) pcm = static_cast<int16_t *>(malloc(bytes));
  return pcm;
}

int16_t *renderClick(const ClickSpec &spec, uint8_t variant, uint8_t seed, uint32_t *framesOut) {
  const float pitch = kPitchJitter[variant];
  const float level = kLevelJitter[variant];
  const float tauMultiplier = kTauJitter[variant];

  uint32_t frames = static_cast<uint32_t>(spec.durMs * 0.001f * static_cast<float>(kRate));
  if (frames < 8) frames = 8;
  int16_t *pcm = allocatePcm(frames);
  if (pcm == nullptr) return nullptr;

  const float toneTau = spec.toneTauMs * 0.001f * tauMultiplier;
  const float noiseTau = spec.noiseTauMs * 0.001f * tauMultiplier;
  const float bodyTau = spec.bodyTauMs * 0.001f * tauMultiplier;
  // Linear tail so a clip whose envelope has not fully decayed still ends at
  // zero. A truncated buffer would add its own click on every playback.
  const uint32_t fadeFrames = frames / 6 + 1;

  uint32_t noiseState = 0x1234ABCDu + static_cast<uint32_t>(seed + 1) * 0x9E3779B9u;
  float lowpass = 0.0f;

  for (uint32_t n = 0; n < frames; ++n) {
    const float t = static_cast<float>(n) / static_cast<float>(kRate);

    // xorshift32 noise in [-1, 1], then a one-pole lowpass for timbre.
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    const float raw = static_cast<float>(static_cast<int32_t>(noiseState)) / 2147483648.0f;
    lowpass += spec.lpAlpha * (raw - lowpass);

    float value =
        spec.noiseLevel * lowpass * expf(-t / noiseTau) +
        spec.toneLevel * sinf(kTwoPi * spec.toneHz * pitch * t) * expf(-t / toneTau) +
        spec.bodyLevel * sinf(kTwoPi * spec.bodyHz * pitch * t) * expf(-t / bodyTau);
    value *= spec.peak * level;
    if (n + fadeFrames >= frames) {
      value *= static_cast<float>(frames - n) / static_cast<float>(fadeFrames);
    }
    pcm[n] = static_cast<int16_t>(clampUnit(value) * kPeak);
  }

  *framesOut = frames;
  return pcm;
}

}  // namespace

bool DeskKeyClick::begin(Print &log) {
  if (ready_) return true;
  uint8_t made = 0;
  uint32_t bytes = 0;
  // Every sound is built even when the stored setting is Off, so switching is
  // instant and costs no allocation on the touch path.
  for (uint8_t s = 0; s < kRealSounds; ++s) {
    for (uint8_t release = 0; release < 2; ++release) {
      for (uint8_t variant = 0; variant < kVariants; ++variant) {
        const uint8_t index = static_cast<uint8_t>((s * 2 + release) * kVariants + variant);
        uint32_t frames = 0;
        int16_t *pcm = renderClick(kSpecs[s][release], variant, index, &frames);
        clips_[index].pcm = pcm;
        clips_[index].frames = pcm != nullptr ? frames : 0;
        if (pcm != nullptr) {
          ++made;
          bytes += frames * sizeof(int16_t);
        }
      }
    }
  }
  ready_ = made == kClipCount;
  log.println(String("[desk-click] synthesized ") + made + "/" + kClipCount + " clips, " + bytes +
              " B at " + kRate + " Hz");
  return ready_;
}

const DeskKeyClick::Clip *DeskKeyClick::pick(bool releaseSound) {
  if (!ready_ || sound_ == kOff) return nullptr;
  // One-bit xorshift rotation so consecutive keystrokes get different variants
  // without a real RNG on the touch path.
  rotate_ ^= rotate_ << 13;
  rotate_ ^= rotate_ >> 17;
  rotate_ ^= rotate_ << 5;
  const uint8_t variant = static_cast<uint8_t>(rotate_ % kVariants);
  const uint8_t base = static_cast<uint8_t>(sound_ - 1);
  const uint8_t index =
      static_cast<uint8_t>((base * 2 + (releaseSound ? 1 : 0)) * kVariants + variant);
  const Clip &clip = clips_[index];
  return clip.pcm != nullptr ? &clip : nullptr;
}

void DeskKeyClick::press(DeskAudioEngine &engine, uint8_t volumePercent) {
  const Clip *clip = pick(false);
  if (clip != nullptr) engine.playClip(clip->pcm, clip->frames, volumePercent);
}

void DeskKeyClick::release(DeskAudioEngine &engine, uint8_t volumePercent) {
  const Clip *clip = pick(true);
  if (clip != nullptr) engine.playClip(clip->pcm, clip->frames, volumePercent);
}

uint32_t DeskKeyClick::bankBytes() const {
  uint32_t bytes = 0;
  for (uint8_t i = 0; i < kClipCount; ++i) bytes += clips_[i].frames * sizeof(int16_t);
  return bytes;
}

#else  // CYPHER_DESK_AUDIO_BACKEND

bool DeskKeyClick::begin(Print &log) {
  (void)log;
  return false;
}
void DeskKeyClick::press(DeskAudioEngine &, uint8_t) {}
void DeskKeyClick::release(DeskAudioEngine &, uint8_t) {}
uint32_t DeskKeyClick::bankBytes() const { return 0; }
const DeskKeyClick::Clip *DeskKeyClick::pick(bool) { return nullptr; }

#endif  // CYPHER_DESK_AUDIO_BACKEND
