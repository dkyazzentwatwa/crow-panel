#ifndef CYPHER_TUNE_SAMPLE_BANK_H
#define CYPHER_TUNE_SAMPLE_BANK_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// One pad's sound plus its performance parameters. `pcm` is a mono 16-bit
// buffer owned by the bank (PSRAM-first). srcRate may differ from the engine
// rate: the voice resampler folds the ratio into baseIncFP, so SD kits load
// at their native rate with no resample pass.
struct PadSound {
  int16_t *pcm = nullptr;
  uint32_t frames = 0;
  uint32_t srcRate = 0;
  uint32_t baseIncFP = 0;    // (srcRate << 16) / engineRate
  uint8_t gain = 255;        // pad volume 0-255
  int8_t pitchSemis = 0;     // -12..+12
  uint8_t chokeGroup = 0;    // 0 = none, 1-4
  uint8_t defaultVelocity = 100;
  char label[12] = "";
  char sampleRef[28] = "";   // "builtin:kick", "sd:tr808/pad01", "none"
};

// Owns the 16 pad sounds of one kit. Two static banks exist so an SD kit can
// be staged in one while the audio task keeps playing the other; the engine
// flips between them at a block boundary (see AudioEngine).
//
// Threading contract: buffers are only adopted/freed from the loop context,
// and never on a bank the audio task currently reads. The gain/pitch/choke
// setters are byte writes and safe while the bank is live.
class SampleBank {
 public:
  static const uint8_t kPadCount = 16;

  void beginDefaults(uint32_t engineRate);
  // Takes ownership of pcm (allocated via allocFrames or malloc-compatible).
  // Frees any previous buffer on that pad.
  bool adoptPcm(uint8_t pad, int16_t *pcm, uint32_t frames, uint32_t srcRate,
                const char *sampleRef);
  void freePcm(uint8_t pad);
  void freeAll();

  const PadSound &pad(uint8_t pad0to15) const;
  bool setGain(uint8_t pad, uint8_t gain);
  bool setPitch(uint8_t pad, int8_t semis);
  bool setChoke(uint8_t pad, uint8_t group);  // 0-4

  const char *kitName() const { return kitName_; }
  void setKitName(const char *name);
  uint32_t engineRate() const { return engineRate_; }
  uint32_t loadedCount() const;  // pads with real PCM
  uint32_t totalBytes() const;
  String sampleMap() const;

  // PSRAM-first sample allocation (internal-heap fallback); free with free().
  static int16_t *allocFrames(uint32_t frames);

 private:
  PadSound pads_[kPadCount];
  char kitName_[16] = "builtin";
  uint32_t engineRate_ = CYPHER_TUNE_ENGINE_RATE;
};

#endif
