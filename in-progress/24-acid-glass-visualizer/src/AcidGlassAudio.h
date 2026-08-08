#ifndef ACID_GLASS_AUDIO_H
#define ACID_GLASS_AUDIO_H

#include "../config/ProjectConfig.h"
#include "AcidGlassTypes.h"

class Print;

class AcidGlassAudio {
 public:
  bool mountAndIndex();
  bool begin();
  void end();
  bool play(uint8_t index);
  void stop();
  void next();
  void previous();
  void setVolume(uint8_t volume);
  void setSensitivity(uint8_t sensitivity);
  AudioFeatures features() const;
  void synthFeatures(uint32_t nowMs, AudioFeatures &out) const;
  void printStatus(Print &out) const;

  bool sdReady() const { return sdReady_; }
  bool audioReady() const { return audioReady_; }
  bool playing() const { return playing_; }
  uint8_t trackCount() const { return trackCount_; }
  uint8_t activeTrack() const { return activeTrack_; }
  const char *trackName(uint8_t index) const;
  uint32_t underruns() const { return underruns_; }
  const char *status() const { return status_; }

 private:
  static constexpr uint8_t kPathLength = 96;
  static constexpr uint16_t kBlockFrames = 256;
  static constexpr uint16_t kReadBufferBytes = 4096;

  char tracks_[ACID_GLASS_MAX_TRACKS][kPathLength] = {};
  uint8_t trackCount_ = 0;
  volatile uint8_t activeTrack_ = 0;
  volatile int16_t requestedTrack_ = -1;
  volatile bool stopRequested_ = false;
  volatile bool playing_ = false;
  volatile bool internalSynth_ = false;
  bool sdReady_ = false;
  bool audioReady_ = false;
  volatile uint8_t volume_ = 70;
  volatile uint8_t sensitivity_ = 170;
  volatile uint32_t underruns_ = 0;
  char status_[96] = "audio disabled";
  void *task_ = nullptr;
  void *txChannel_ = nullptr;
  uint32_t synthSampleClock_ = 0;
  uint32_t synthBassPhase_ = 0;
  uint32_t synthKickPhase_ = 0;
  uint32_t synthNoise_ = 0xA51D6124;

  mutable portMUX_TYPE featureMux_ = portMUX_INITIALIZER_UNLOCKED;
  AudioFeatures features_;

  static void taskEntry_(void *self);
  void taskLoop_();
  bool openTrack_(uint8_t index);
  void closeTrack_();
  bool readSourceFrame_(int16_t &left, int16_t &right);
  int readByte_();
  void renderInternalSynth_(int16_t *output, int16_t *mono, uint16_t frames);
  void analyze_(const int16_t *mono, uint16_t frames);
};

#endif
