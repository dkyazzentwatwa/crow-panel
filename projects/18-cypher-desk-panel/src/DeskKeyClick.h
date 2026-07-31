#ifndef CYPHER_DESK_KEY_CLICK_H
#define CYPHER_DESK_KEY_CLICK_H

#include "../config/ProjectConfig.h"
#include "DeskAudioEngine.h"
#include <Arduino.h>

// Synthesized typing sounds for the on-screen keyboard.
//
// Ported from project 21's KeyAudio, which is hardware-verified: each click is
// three exponentially decaying layers summed - a filtered noise transient, a
// resonant tone, and a low body thud. The specs here are retuned for this
// project's own sound names (Pencil / Typewriter / Mechanical) rather than
// project 21's switch names, because those are what `sound key <0-3>` and the
// Writer settings screen have always called them.
//
// The whole bank is rendered into PSRAM once at boot (3 sounds x press/release
// x 3 variants = 18 clips, ~30 KB at 44.1 kHz) so the touch path only ever
// hands the mixer a pointer. Variants exist so a fast typist never hears the
// same waveform twice in a row.

class DeskKeyClick {
 public:
  enum Sound : uint8_t { kOff = 0, kPencil = 1, kTypewriter = 2, kMechanical = 3 };
  static constexpr uint8_t kSoundCount = 4;
  static constexpr uint8_t kVariants = 3;

  // Renders the bank. Safe to call in a silent build; it simply does nothing.
  bool begin(Print &log);
  bool ready() const;

  void setSound(uint8_t sound);  // 0..3, clamped
  uint8_t sound() const;
  const char *soundName() const;
  static const char *soundName(uint8_t sound);

  // Plays through the engine's one-shot voices. Called from the touch path, so
  // it resolves a pointer and returns - no synthesis, no float math.
  void press(DeskAudioEngine &engine, uint8_t volumePercent);
  void release(DeskAudioEngine &engine, uint8_t volumePercent);

  uint32_t bankBytes() const;

 private:
  // [sound - 1][0 = press, 1 = release][variant]
  static constexpr uint8_t kRealSounds = kSoundCount - 1;
  static constexpr uint8_t kClipCount = kRealSounds * 2 * kVariants;
  struct Clip {
    int16_t *pcm = nullptr;
    uint32_t frames = 0;
  };
  Clip clips_[kClipCount];
  uint8_t sound_ = kPencil;
  bool ready_ = false;
  uint32_t rotate_ = 0x9E3779B9u;

  const Clip *pick(bool releaseSound);
};

#endif
