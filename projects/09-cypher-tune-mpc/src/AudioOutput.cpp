#include "AudioOutput.h"

#if USE_AUDIO && defined(ARDUINO_ARCH_ESP32) && defined(__has_include)
#if __has_include(<ESP_I2S.h>)
#define CYPHER_TUNE_HAS_ESP_I2S 1
#endif
#endif

#ifndef CYPHER_TUNE_HAS_ESP_I2S
#define CYPHER_TUNE_HAS_ESP_I2S 0
#endif

#if CYPHER_TUNE_HAS_ESP_I2S
#include <ESP_I2S.h>
#endif

namespace {

class SilentAudioOutput : public AudioOutput {
 public:
  bool begin(const HardwareProfile &profile, Stream &log) override {
    profile_ = &profile;
#if USE_AUDIO
    log.println(F("[audio] USE_AUDIO=1 but ESP_I2S is unavailable; using compile-safe stub"));
#else
    log.println(F("[audio] USE_AUDIO=0; silent stub active"));
#endif
    return true;
  }

  bool trigger(uint8_t pad, uint8_t velocity) override {
    lastPad_ = pad;
    lastVelocity_ = velocity;
    return true;
  }

  void tick() override {}

  const char *modeName() const override {
#if USE_AUDIO
    return "audio-stub";
#else
    return "silent-stub";
#endif
  }

  bool hardwareReady() const override { return false; }

  String statusLine() const override {
    String status = String(modeName()) + " ready=0";
    if (profile_ != nullptr) {
      status += " bclk=" + String(profile_->audio.bclk);
      status += " lrclk=" + String(profile_->audio.lrclk);
      status += " data=" + String(profile_->audio.sdata);
    }
    status += " lastPad=" + String(lastPad_);
    status += " velocity=" + String(lastVelocity_);
    return status;
  }

 private:
  const HardwareProfile *profile_ = nullptr;
  uint8_t lastPad_ = 0;
  uint8_t lastVelocity_ = 0;
};

#if CYPHER_TUNE_HAS_ESP_I2S
class I2SAudioOutput : public AudioOutput {
 public:
  bool begin(const HardwareProfile &profile, Stream &log) override {
    profile_ = &profile;
    pinMode(profile.audio.control, OUTPUT);
    digitalWrite(profile.audio.control, HIGH);
    i2s_.setPins(profile.audio.bclk, profile.audio.lrclk, profile.audio.sdata);
    ready_ = i2s_.begin(I2S_MODE_STD, CYPHER_TUNE_AUDIO_SAMPLE_RATE,
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    log.print(F("[audio] ESP_I2S std tx "));
    log.println(ready_ ? F("ready") : F("init failed"));
    return ready_;
  }

  bool trigger(uint8_t pad, uint8_t velocity) override {
    lastPad_ = pad;
    lastVelocity_ = velocity;
    if (!ready_) {
      return false;
    }

    int16_t frames[CYPHER_TUNE_AUDIO_CLICK_FRAMES * 2];
    uint16_t toneDivisor = 2 + (pad % 5);
    int16_t base = (int16_t)constrain((int)velocity * CYPHER_TUNE_AUDIO_VOLUME, 0, 12000);
    for (uint16_t i = 0; i < CYPHER_TUNE_AUDIO_CLICK_FRAMES; i++) {
      int16_t envelope = (int16_t)((int32_t)base *
                                   (CYPHER_TUNE_AUDIO_CLICK_FRAMES - i) /
                                   CYPHER_TUNE_AUDIO_CLICK_FRAMES);
      int16_t sample = ((i / toneDivisor) % 2 == 0) ? envelope : -envelope;
      frames[i * 2] = sample;
      frames[i * 2 + 1] = sample;
    }

    size_t bytes = sizeof(frames);
    return i2s_.write((const uint8_t *)frames, bytes) == bytes;
  }

  void tick() override {}

  const char *modeName() const override { return "esp-i2s-click"; }

  bool hardwareReady() const override { return ready_; }

  String statusLine() const override {
    String status = String(modeName()) + " ready=" + String(ready_ ? 1 : 0);
    if (profile_ != nullptr) {
      status += " bclk=" + String(profile_->audio.bclk);
      status += " lrclk=" + String(profile_->audio.lrclk);
      status += " data=" + String(profile_->audio.sdata);
      status += " amp=" + String(profile_->audio.control);
    }
    status += " rate=" + String(CYPHER_TUNE_AUDIO_SAMPLE_RATE);
    status += " lastPad=" + String(lastPad_);
    return status;
  }

 private:
  I2SClass i2s_;
  const HardwareProfile *profile_ = nullptr;
  bool ready_ = false;
  uint8_t lastPad_ = 0;
  uint8_t lastVelocity_ = 0;
};
#endif

}  // namespace

AudioOutput &audioOutput() {
#if CYPHER_TUNE_HAS_ESP_I2S
  static I2SAudioOutput output;
#else
  static SilentAudioOutput output;
#endif
  return output;
}
