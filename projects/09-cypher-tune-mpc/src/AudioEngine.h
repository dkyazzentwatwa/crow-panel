#ifndef CYPHER_TUNE_AUDIO_ENGINE_H
#define CYPHER_TUNE_AUDIO_ENGINE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "SampleBank.h"
#include "Sequencer.h"

// Polyphonic PCM engine over the NS4168 I2S amp, replacing the old
// trigger-a-click AudioOutput. A FreeRTOS render task on core 0 mixes
// CYPHER_TUNE_VOICES sample voices + a metronome lane into small blocks and
// paces itself on the blocking IDF i2s_channel_write. The task is also the
// musical clock: it counts output frames and fires sequencer steps sample-
// accurately at swing-adjusted boundaries (the Arduino ESP_I2S wrapper is
// not used - its DMA ring is hardcoded to ~65 ms of latency; the IDF
// i2s_std driver lets us size it to ~23 ms).
//
// Threading: the loop context (serial + touch) is the ONLY producer of
// commands and the only consumer of events. Multi-field transport actions go
// through the SPSC command ring; BPM/swing/pattern/metronome/record are
// byte-atomic Sequencer fields the render task reads directly.
//
// Without USE_AUDIO (or on cores without the IDF I2S header) the class
// compiles as a silent stub with the same API and running() == false, and
// the sketch falls back to Sequencer::tickMillis().

enum EngineCommandType : uint8_t {
  kCmdTrigger = 0,  // a = pad 0-15, b = velocity 1-127
  kCmdPlay,
  kCmdStop,
  kCmdKitSwap,      // a = bank index (0/1) to make active
  kCmdChokeAll,     // fade every voice (panic)
};

struct EngineCommand {
  uint8_t type;
  uint8_t a;
  uint8_t b;
};

enum EngineEventType : uint8_t {
  kEvtStep = 0,     // step fired (even when empty; playhead cadence)
  kEvtTrigger,      // a voice actually started (sequencer or live pad)
  kEvtRecorded,     // live hit quantized into the pattern
  kEvtKitSwapped,   // bank flip done; `pad` = retired bank index, safe to free
};

struct EngineEvent {
  uint8_t type;
  uint8_t step;
  uint8_t pad;
  uint8_t vel;
};

class AudioEngine {
 public:
  static const uint8_t kVoices = CYPHER_TUNE_VOICES;

  bool begin(const HardwareProfile &profile, SampleBank *bankA, SampleBank *bankB,
             Sequencer *seq, Stream &log);
  bool running() const { return running_; }

  bool post(const EngineCommand &cmd);  // loop context only
  bool nextEvent(EngineEvent &out);     // loop context only

  SampleBank *activeBank();             // bank the voices currently read
  uint8_t activeBankIndex() const { return activeBankIdx_; }
  uint8_t activeVoices() const;
  uint32_t underruns() const { return underruns_; }
  uint32_t framesRendered() const { return framesRendered_; }

  // Live output scope/VU. The render task decimates the post-clip mix into a
  // ring and publishes each block's peak; the UI reads them unlocked, since a
  // torn read costs at most one visually-odd frame and never a crash. Both
  // return 0 in silent builds, which is how the UI knows to fall back to the
  // simulated voice meters.
  static const uint16_t kScopeSize = 256;
  uint8_t outputPeak() const { return outPeak_; }
  // Copies up to `max` samples oldest-to-newest ending at the newest write.
  uint16_t copyScope(int16_t *out, uint16_t max) const;

  const char *modeName() const;
  bool hardwareReady() const { return running_; }
  String statusLine() const;

 private:
  struct Voice {
    const int16_t *pcm = nullptr;
    uint32_t frames = 0;
    uint32_t posFP = 0;      // 16.16 sample position
    uint32_t incFP = 0;      // 16.16 increment (rate ratio x pitch)
    int32_t gainQ12 = 0;     // current gain (ramps during fade)
    int32_t fadeDecQ12 = 0;  // per-frame decrement while fading
    uint8_t attack = 0;      // ramp-in frames left (declick)
    uint8_t pad = 0;
    uint8_t chokeGroup = 0;
    bool active = false;
    uint32_t startedAt = 0;  // framesRendered_ stamp, for oldest-steal
  };

  // Render-task internals (compiled only in audio builds; harmless members
  // otherwise so the class layout is flag-independent per translation unit).
  void renderTask_();
  static void renderTaskTrampoline_(void *self);
  void drainCommands_();
  void fireStep_();
  void startVoice_(uint8_t pad, uint8_t velocity, bool fromSequencer);
  void startMetro_(bool accent);
  void chokeGroup_(uint8_t group);
  void fadeAll_();
  void mixChunk_(int32_t *acc, uint32_t frames);
  bool pushEvent_(const EngineEvent &evt);

  SampleBank *banks_[2] = {nullptr, nullptr};
  Sequencer *seq_ = nullptr;
  volatile uint8_t activeBankIdx_ = 0;
  volatile bool running_ = false;

  Voice voices_[kVoices + 1];  // +1 = metronome lane (never stolen/choked)
  int16_t *metroAccent_ = nullptr;
  int16_t *metroTick_ = nullptr;
  uint32_t metroAccentFrames_ = 0;
  uint32_t metroTickFrames_ = 0;

  // Frame clock.
  uint32_t framesToNextStep_ = 0;
  uint32_t curStepFrames_ = 0;

  // SPSC rings (loop -> task and task -> loop). Power-of-two sizes.
  static const uint16_t kCmdRingSize = 64;
  static const uint16_t kEvtRingSize = 128;
  EngineCommand cmdRing_[kCmdRingSize];
  EngineEvent evtRing_[kEvtRingSize];
  volatile uint16_t cmdHead_ = 0, cmdTail_ = 0;
  volatile uint16_t evtHead_ = 0, evtTail_ = 0;

  volatile uint32_t underruns_ = 0;
  volatile uint32_t framesRendered_ = 0;
  volatile uint8_t activeVoiceCount_ = 0;

  // Scope ring (render task writes, UI reads). Power-of-two size.
  volatile int16_t scope_[kScopeSize];
  volatile uint16_t scopeWrite_ = 0;
  volatile uint8_t outPeak_ = 0;

  void *txChan_ = nullptr;  // i2s_chan_handle_t, opaque here
  void *task_ = nullptr;    // TaskHandle_t
  // Mix buffers live in internal SRAM as class members (the object is a
  // global): int32 accumulate + int16 stereo out per block.
  int32_t acc_[CYPHER_TUNE_BLOCK_FRAMES];
  int16_t out_[CYPHER_TUNE_BLOCK_FRAMES * 2];
};

AudioEngine &audioEngine();

#endif
