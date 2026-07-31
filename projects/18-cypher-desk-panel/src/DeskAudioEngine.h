#ifndef CYPHER_DESK_AUDIO_ENGINE_H
#define CYPHER_DESK_AUDIO_ENGINE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// The one I2S owner for this project.
//
// Project 18 used to carry three I2SClass instances - one in DeskAudio for the
// Writer, two in DeskAudioService for the OS speaker and microphone - all on
// the Arduino ESP_I2S wrapper, all pumped 128 frames at a time from a loop()
// that ended in delay(12). The wrapper's hardcoded ~65 ms DMA ring plus a pump
// that woke slower than the ring drained is why audio here was never claimed.
//
// This replaces all of it with the raw IDF i2s_std path that projects 09, 20,
// 21 and 22 are hardware-verified on:
//
//   - ONE TX channel at a fixed CYPHER_DESK_AUDIO_OUT_RATE, stereo, 16-bit.
//     Every source is resampled up to it, so music at 44.1 kHz, a 16 kHz
//     ambience loop and a key click can share the output without ever
//     reconfiguring the clock mid-playback.
//   - A FreeRTOS mixer task pinned to core 0. The blocking i2s_channel_write
//     paces it; nothing else in the sketch has to be timely for audio to be
//     continuous.
//   - One streaming voice fed from a PSRAM ring, plus four one-shot voices.
//
// THREADING INVARIANT: the mixer task NEVER touches SD_MMC, or any file. Loop
// context reads the card and pushes converted frames into the ring; the task
// only drains it. Breaking this deadlocks audio behind a slow card.

// The backend covers BOTH directions: a recorder-only build (no speaker) still
// needs the IDF driver for the PDM microphone. Gating it on USE_CYPHER_DESK_AUDIO
// alone would compile the recorder-guard row green while leaving it unable to
// record a single byte.
#if !defined(CYPHER_DESK_AUDIO_BACKEND)
#if (USE_CYPHER_DESK_AUDIO || USE_CYPHER_DESK_RECORDER) && __has_include(<driver/i2s_std.h>)
// driver/i2s_std.h is an ESP-IDF core header, not an Arduino library, so the
// "__has_include silently kills library linkage" trap does not apply here.
#define CYPHER_DESK_AUDIO_BACKEND 1
#else
#define CYPHER_DESK_AUDIO_BACKEND 0
#endif
#endif

class DeskAudioEngine {
 public:
  static constexpr uint32_t kOutputRate = CYPHER_DESK_AUDIO_OUT_RATE;
  static constexpr uint8_t kOneShotVoices = 4;

  bool begin(Print &log);
  void end();
  bool ready() const;
  String status() const;

  // --- Streaming voice -----------------------------------------------------
  // One at a time by design: DeskAudioService already arbitrates between the
  // Writer's ambience, the Music app and the recorder, so a second streaming
  // voice would have no owner that could ask for it.

  // Configure the converter for the source about to be pushed. Any rate from
  // DeskWav::kMinRate to kMaxRate, mono or stereo, 8- or 16-bit.
  bool openStream(uint32_t sourceRate, uint16_t channels, uint16_t bits);
  // Converts and enqueues as much as fits, returning bytes consumed. A short
  // return means the ring is full - keep the remainder and call again.
  size_t pushStream(const uint8_t *bytes, size_t length);
  // Room left, in source frames, so a caller can size its next SD read.
  uint32_t streamFreeSourceFrames() const;
  uint32_t streamQueuedFrames() const;
  // Stop now and drop whatever is queued.
  void closeStream();
  // No more data is coming; let the queue play out, then go idle.
  void endStreamInput();
  bool streamActive() const;
  // Output frames of the CURRENT stream already written to I2S. This is the
  // master clock the video player presents frames against.
  uint64_t streamPlayedFrames() const;
  uint32_t streamUnderruns() const;

  // --- One-shot voices (key clicks, cues, test tone) -----------------------
  // pcm must be mono at kOutputRate and must stay alive for the clip's
  // duration - the boot-synthesized click bank is permanent, so that is free.
  void playClip(const int16_t *pcm, uint32_t frames, uint8_t gainPercent);
  void stopClips();

  // --- Levels --------------------------------------------------------------
  void setVolume(uint8_t percent);  // applies to the streaming voice
  uint8_t volume() const;

  // --- Microphone (PDM RX, recorder only) ----------------------------------
  bool beginMicrophone();
  void endMicrophone();
  bool microphoneReady() const;
  size_t readMicrophone(uint8_t *destination, size_t length);

 private:
#if CYPHER_DESK_AUDIO_BACKEND
  static void mixerTrampoline(void *self);
  void mixerTask();
  void setAmp(bool on);
  bool emitFrame(int16_t left, int16_t right);

  void *txChannel_ = nullptr;
  void *rxChannel_ = nullptr;
  void *task_ = nullptr;
  bool running_ = false;
  bool micReady_ = false;
  uint8_t ampPin_ = 0;
  bool ampActiveHigh_ = false;

  // SPSC ring of ready-to-play output frames. Free-running indices with a
  // power-of-two capacity, so wraparound needs no special case: the unsigned
  // difference is always the live count.
  int16_t *ring_ = nullptr;
  uint32_t ringFrames_ = 0;
  uint32_t ringMask_ = 0;
  volatile uint32_t ringWrite_ = 0;
  volatile uint32_t ringRead_ = 0;

  // Converter state, loop context only.
  uint32_t sourceRate_ = 0;
  uint16_t sourceChannels_ = 0;
  uint16_t sourceBits_ = 0;
  uint32_t phase_ = 0;      // Q16 position between prevFrame and the next one
  uint32_t phaseStep_ = 0;  // Q16 sourceRate / kOutputRate
  int16_t prevLeft_ = 0;
  int16_t prevRight_ = 0;
  bool havePrev_ = false;
  uint8_t partial_[4] = {};  // bytes of a source frame split across two pushes
  uint8_t partialLength_ = 0;

  volatile bool streamOpen_ = false;
  volatile bool streamEnding_ = false;
  volatile uint64_t streamPlayed_ = 0;
  volatile uint32_t underruns_ = 0;

  struct OneShot {
    const int16_t *pcm = nullptr;
    uint32_t frames = 0;
    uint32_t position = 0;
    uint16_t gainQ12 = 4096;
    volatile bool active = false;
  };
  OneShot shots_[kOneShotVoices];
  volatile uint8_t nextShot_ = 0;
#endif
  uint8_t volume_ = 70;
  String status_ = "audio disabled";
};

#endif
