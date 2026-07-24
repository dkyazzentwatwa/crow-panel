#ifndef CYPHER_BOY_AUDIO_H
#define CYPHER_BOY_AUDIO_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Game Boy APU output to the panel's NS4168 I2S amplifier.
//
// Follows the path proven by project 09 (Cypher Tune MPC):
//   * IDF `driver/i2s_std.h` directly, NOT the Arduino ESP_I2S wrapper - that
//     wrapper hardcodes a ~65 ms DMA ring, which is far too much latency for
//     something you are pressing buttons at.
//   * Stream silence BEFORE raising the amp enable, so the amp wakes onto a
//     clean bus instead of a pop.
//   * The amp enable (IO30 on this board) is ACTIVE-LOW - polarity comes from
//     HardwareProfile, never hardcoded here.
//
// gnuboy is initialised with GB_AUDIO_STEREO_S16 so its mixer emits interleaved
// stereo that can go straight to I2S with no conversion. Its audio callback
// fires once per emulated frame; we write from there with a BOUNDED timeout so
// the audio paces the emulator to real time without ever being able to hang it.
//
// Fail-soft by design: if any of this fails to come up, ready() stays false,
// nothing is ever written, and the emulator keeps running silently.
class GbAudio {
 public:
  bool begin();
  void shutdown();  // mute the amp and release the channel

  // Called from gnuboy's audio callback. `samples` counts int16 values
  // (stereo frames * 2). Applies volume, then writes to I2S.
  void submit(const int16_t *data, size_t samples);

  void setVolume(uint8_t v);  // 0..255
  uint8_t volume() const { return volume_; }
  void setMuted(bool m);
  bool muted() const { return muted_; }

  // Synthesized UI sounds. Both are blocking but short, and both are no-ops
  // when muted or when I2S never came up, so callers need no guards.
  void playChime();  // ~450 ms two-note startup tone for the splash
  void playClick();  // ~10 ms tick for touch feedback

  bool ready() const { return ready_; }
  uint32_t underruns() const { return underruns_; }
  const String &status() const { return status_; }

 private:
  void ampEnable(bool on);

  void *txChan_ = nullptr;   // i2s_chan_handle_t
  int16_t *scratch_ = nullptr;
  size_t scratchSamples_ = 0;
  uint8_t volume_ = 180;
  bool muted_ = false;
  bool ready_ = false;
  uint8_t ampPin_ = 0;
  bool ampActiveHigh_ = false;
  uint32_t underruns_ = 0;
  String status_;
};

#endif
