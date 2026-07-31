#include "AudioEngine.h"

#if USE_AUDIO && __has_include(<driver/i2s_std.h>)

#include <driver/i2s_std.h>
#include <math.h>
#include "SynthKit.h"

namespace {

constexpr uint32_t kRate = CYPHER_TUNE_ENGINE_RATE;
constexpr uint32_t kBlockFrames = CYPHER_TUNE_BLOCK_FRAMES;
constexpr uint32_t kRingFrames = (uint32_t)CYPHER_TUNE_BLOCK_FRAMES * CYPHER_TUNE_DMA_DESC;
constexpr uint8_t kAttackFrames = 16;  // declick ramp-in
constexpr uint8_t kFadeFrames = 64;    // choke/steal/stop ramp-out
// Scope decimation: 128-frame blocks / 8 = 16 samples per block, so the
// 256-entry ring holds ~90 ms of output - enough to see a drum transient.
constexpr uint32_t kScopeDecim = CYPHER_TUNE_BLOCK_FRAMES / 16 > 0
                                     ? CYPHER_TUNE_BLOCK_FRAMES / 16
                                     : 1;

// Pitch ratio 2^(semis/12) in Q16, semis -12..+12, indexed by semis + 12.
// This replaces a powf() call that ran on the render task once per voice
// start: a ~170-byte libm routine plus a float pow on a real-time path, for
// 25 possible answers. A table lookup and one 64-bit multiply is both faster
// and smaller.
//
// DRAM_ATTR keeps the table out of flash. That is worth doing on its own -
// but note it does NOT make the render path cache-safe: renderTask_,
// mixChunk_ and startVoice_ all live in .flash.text, so a genuinely
// cache-disabled window would stall the task regardless of this table. The
// realistic symptom of an NVS write during playback is a DMA underrun, not a
// fault. Making the path truly cache-independent means IRAM_ATTR across the
// whole render chain, which is a separate change.
DRAM_ATTR const uint32_t kPitchRatioQ16[25] = {
     32768,  34716,  36781,  38968,  41285,
     43740,  46341,  49097,  52016,  55109,
     58386,  61858,  65536,  69433,  73562,
     77936,  82570,  87480,  92682,  98193,
    104032, 110218, 116772, 123715, 131072,
};

}  // namespace

bool AudioEngine::begin(const HardwareProfile &profile, SampleBank *bankA,
                        SampleBank *bankB, Sequencer *seq, Stream &log) {
  banks_[0] = bankA;
  banks_[1] = bankB;
  seq_ = seq;

  // IDF i2s_std channel with a small DMA ring. The Arduino ESP_I2S wrapper
  // hardcodes dma_desc_num=6/dma_frame_num=240 (~65 ms of queued audio at
  // 22.05 kHz) which is unplayable for finger drumming; sizing the ring here
  // gets pad-to-speaker under ~30 ms.
  i2s_chan_config_t chanCfg = {};
  chanCfg.id = I2S_NUM_AUTO;
  chanCfg.role = I2S_ROLE_MASTER;
  chanCfg.dma_desc_num = CYPHER_TUNE_DMA_DESC;
  chanCfg.dma_frame_num = kBlockFrames;
  chanCfg.auto_clear = true;
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
    log.println(F("[engine] i2s_new_channel failed"));
    return false;
  }

  // Clock/slot/pin config transcribed from ESP_I2S's initSTD (Philips slot,
  // 16-bit stereo, no MCLK - the NS4168 derives everything from BCLK/LRCLK).
  i2s_std_config_t stdCfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),
      .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = (gpio_num_t)I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)profile.audio.bclk,
              .ws = (gpio_num_t)profile.audio.lrclk,
              .dout = (gpio_num_t)profile.audio.sdata,
              .din = (gpio_num_t)I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
          },
  };
  if (i2s_channel_init_std_mode(tx, &stdCfg) != ESP_OK ||
      i2s_channel_enable(tx) != ESP_OK) {
    log.println(F("[engine] i2s std init/enable failed"));
    i2s_del_channel(tx);
    return false;
  }
  txChan_ = tx;

  metroAccent_ = SynthKit::synthesizeMetronome(true, kRate, &metroAccentFrames_);
  metroTick_ = SynthKit::synthesizeMetronome(false, kRate, &metroTickFrames_);

  // Stream silence before raising the amp enable so it wakes to a clean bus.
  memset(out_, 0, sizeof(out_));
  size_t written = 0;
  for (uint8_t i = 0; i < 2; i++) {
    i2s_channel_write(tx, out_, sizeof(out_), &written, portMAX_DELAY);
  }
  pinMode(profile.audio.control, OUTPUT);
  digitalWrite(profile.audio.control, profile.audio.controlActiveHigh ? HIGH : LOW);

  running_ = true;
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(renderTaskTrampoline_, "mpc-audio", 8192, this,
                              CYPHER_TUNE_AUDIO_TASK_PRIO, &task,
                              CYPHER_TUNE_AUDIO_TASK_CORE) != pdPASS) {
    running_ = false;
    log.println(F("[engine] render task create failed"));
    return false;
  }
  task_ = task;
  log.println(String("[engine] i2s up: ") + String(kRate) + "Hz block=" +
              String(kBlockFrames) + "x" + String(CYPHER_TUNE_DMA_DESC) +
              " ring=" + String(kRingFrames * 1000 / kRate) + "ms voices=" +
              String(kVoices));
  return true;
}

void AudioEngine::renderTaskTrampoline_(void *self) {
  static_cast<AudioEngine *>(self)->renderTask_();
}

void AudioEngine::renderTask_() {
  i2s_chan_handle_t tx = (i2s_chan_handle_t)txChan_;
  uint32_t lastWriteDoneUs = micros();
  const uint32_t ringUs = kRingFrames * 1000000ULL / kRate;

  for (;;) {
    drainCommands_();

    memset(acc_, 0, sizeof(acc_));
    uint32_t offset = 0;
    uint32_t remaining = kBlockFrames;
    while (remaining > 0) {
      uint32_t chunk = remaining;
      if (seq_->playing()) {
        if (framesToNextStep_ == 0) {
          fireStep_();
        }
        if (framesToNextStep_ < chunk) {
          chunk = framesToNextStep_;
        }
      }
      mixChunk_(acc_ + offset, chunk);
      if (seq_->playing()) {
        framesToNextStep_ -= chunk;
      }
      offset += chunk;
      remaining -= chunk;
    }

    // Clip to stereo, and on the same pass publish the UI's scope/VU data:
    // block peak plus every kScopeDecim'th sample into the ring.
    int32_t peak = 0;
    uint16_t scopeW = scopeWrite_;
    for (uint32_t n = 0; n < kBlockFrames; n++) {
      int32_t v = acc_[n];
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      out_[2 * n] = (int16_t)v;
      out_[2 * n + 1] = (int16_t)v;
      int32_t mag = v < 0 ? -v : v;
      if (mag > peak) {
        peak = mag;
      }
      if ((n % kScopeDecim) == 0) {
        scope_[scopeW & (kScopeSize - 1)] = (int16_t)v;
        scopeW++;
      }
    }
    scopeWrite_ = scopeW;
    outPeak_ = (uint8_t)((peak * 255) / 32767);

    size_t written = 0;
    i2s_channel_write(tx, out_, sizeof(out_), &written, portMAX_DELAY);
    // If more than a full DMA ring of wall time passed between write
    // completions, the DMA ran dry and the output glitched.
    uint32_t now = micros();
    if ((uint32_t)(now - lastWriteDoneUs) > ringUs + ringUs / 2) {
      underruns_ = underruns_ + 1;
    }
    lastWriteDoneUs = now;
    framesRendered_ = framesRendered_ + kBlockFrames;

    uint8_t activeNow = 0;
    for (uint8_t i = 0; i < kVoices; i++) {
      if (voices_[i].active) {
        activeNow++;
      }
    }
    activeVoiceCount_ = activeNow;
  }
}

void AudioEngine::drainCommands_() {
  while (cmdTail_ != cmdHead_) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    EngineCommand cmd = cmdRing_[cmdTail_];
    cmdTail_ = (uint16_t)((cmdTail_ + 1) & (kCmdRingSize - 1));
    switch (cmd.type) {
      case kCmdTrigger: {
        uint8_t vel = cmd.b;
        if (vel == 0) {
          vel = banks_[activeBankIdx_]->pad(cmd.a).defaultVelocity;
        }
        if (seq_->recording()) {
          uint8_t target = 0;
          if (seq_->playing() && curStepFrames_ != 0) {
            target = seq_->quantizedStep(curStepFrames_ - framesToNextStep_,
                                         curStepFrames_);
          }
          seq_->setVel(seq_->pattern(), target, cmd.a, vel);
          pushEvent_({kEvtRecorded, target, cmd.a, vel});
        }
        startVoice_(cmd.a, vel, false);
        break;
      }
      case kCmdPlay:
        seq_->play();
        framesToNextStep_ = 0;
        curStepFrames_ = 0;
        // Restart the backing loop from its downbeat so the pattern and the
        // loop always begin together on beat 1.
        loopPos_ = 0;
        break;
      case kCmdStop:
        seq_->stop();
        fadeAll_();
        loopPos_ = 0;
        break;
      case kCmdKitSwap: {
        // Kill every voice before the flip so nothing can read buffers the
        // loop is about to free; the swap event tells it the retired index.
        for (uint8_t i = 0; i <= kVoices; i++) {
          voices_[i].active = false;
        }
        uint8_t retired = activeBankIdx_;
        activeBankIdx_ = cmd.a & 1;
        pushEvent_({kEvtKitSwapped, 0, retired, 0});
        break;
      }
      case kCmdLoopSwap: {
        // Adopt the staged buffer at a block boundary; the displaced one is
        // handed back through takeRetiredLoop() once the loop context sees
        // kEvtLoopSwapped, so it is never freed while this task can read it.
        loopRetired_ = loopPcm_;
        loopPcm_ = loopPending_;
        loopFrames_ = loopPendingFrames_;
        loopIncFP_ = loopPendingIncFP_;
        loopPending_ = nullptr;
        loopPendingFrames_ = 0;
        loopPendingIncFP_ = 0;
        loopPos_ = 0;
        loopPosFP_ = 0;
        pushEvent_({kEvtLoopSwapped, 0, 0, 0});
        break;
      }
      case kCmdLoopClear:
        loopRetired_ = loopPcm_;
        loopPcm_ = nullptr;
        loopFrames_ = 0;
        loopIncFP_ = 0;
        loopPos_ = 0;
        loopPosFP_ = 0;
        pushEvent_({kEvtLoopSwapped, 0, 0, 0});
        break;
      case kCmdChokeAll:
        fadeAll_();
        break;
      default:
        break;
    }
  }
}

void AudioEngine::fireStep_() {
  uint8_t step = seq_->advancePlayStep();
  pushEvent_({kEvtStep, step, 0, 0});
  uint8_t pattern = seq_->pattern();
  for (uint8_t pad = 0; pad < Sequencer::kPads; pad++) {
    uint8_t vel = seq_->vel(pattern, step, pad);
    if (vel != 0) {
      startVoice_(pad, vel, true);
    }
  }
  if (seq_->metronome() && step % 4 == 0) {
    startMetro_(step == 0);
  }
  curStepFrames_ = seq_->stepDurationFrames(step, kRate);
  framesToNextStep_ = curStepFrames_;
}

void AudioEngine::startVoice_(uint8_t pad, uint8_t velocity, bool fromSequencer) {
  (void)fromSequencer;
  const PadSound &sound = banks_[activeBankIdx_]->pad(pad);
  pushEvent_({kEvtTrigger, seq_->playStep(), pad, velocity});

  if (sound.chokeGroup != 0) {
    chokeGroup_(sound.chokeGroup);
  }
  if (sound.pcm == nullptr || sound.frames < 2) {
    return;  // nothing loaded on this pad (visual feedback still happened)
  }

  // Free slot, else steal the oldest (ramp-in masks the cut).
  Voice *voice = nullptr;
  for (uint8_t i = 0; i < kVoices; i++) {
    if (!voices_[i].active) {
      voice = &voices_[i];
      break;
    }
  }
  if (voice == nullptr) {
    uint32_t oldest = 0xFFFFFFFF;
    for (uint8_t i = 0; i < kVoices; i++) {
      if (voices_[i].startedAt < oldest) {
        oldest = voices_[i].startedAt;
        voice = &voices_[i];
      }
    }
  }

  // Clamp defensively: pitchSemis is documented -12..+12 and SampleBank
  // enforces it, but this indexes a fixed table on the audio task.
  int32_t semis = sound.pitchSemis;
  if (semis < -12) semis = -12;
  if (semis > 12) semis = 12;
  uint32_t pitchQ16 = kPitchRatioQ16[semis + 12];

  uint64_t gain = (uint64_t)velocity * sound.gain * masterVolume_;
  voice->pcm = sound.pcm;
  voice->frames = sound.frames;
  voice->posFP = 0;
  voice->incFP = (uint32_t)(((uint64_t)sound.baseIncFP * pitchQ16) >> 16);
  voice->gainQ12 = (int32_t)((gain << 12) / (127ULL * 255 * 255));
  voice->fadeDecQ12 = 0;
  voice->attack = kAttackFrames;
  voice->pad = pad;
  voice->chokeGroup = sound.chokeGroup;
  voice->startedAt = framesRendered_;
  voice->active = true;
}

void AudioEngine::startMetro_(bool accent) {
  Voice &voice = voices_[kVoices];
  voice.pcm = accent ? metroAccent_ : metroTick_;
  voice.frames = accent ? metroAccentFrames_ : metroTickFrames_;
  if (voice.pcm == nullptr) {
    return;
  }
  voice.posFP = 0;
  voice.incFP = 1 << 16;
  voice.gainQ12 = (int32_t)((uint32_t)masterVolume_ << 12) / 255;
  voice.fadeDecQ12 = 0;
  voice.attack = 4;
  voice.pad = 0xFF;
  voice.chokeGroup = 0;
  voice.startedAt = framesRendered_;
  voice.active = true;
}

void AudioEngine::chokeGroup_(uint8_t group) {
  for (uint8_t i = 0; i < kVoices; i++) {
    Voice &voice = voices_[i];
    if (voice.active && voice.chokeGroup == group && voice.fadeDecQ12 == 0) {
      voice.fadeDecQ12 = voice.gainQ12 / kFadeFrames;
      if (voice.fadeDecQ12 == 0) {
        voice.fadeDecQ12 = 1;
      }
    }
  }
}

void AudioEngine::fadeAll_() {
  for (uint8_t i = 0; i <= kVoices; i++) {
    Voice &voice = voices_[i];
    if (voice.active && voice.fadeDecQ12 == 0) {
      voice.fadeDecQ12 = voice.gainQ12 / kFadeFrames;
      if (voice.fadeDecQ12 == 0) {
        voice.fadeDecQ12 = 1;
      }
    }
  }
}

void AudioEngine::mixChunk_(int32_t *acc, uint32_t frames) {
  // Backing loop first, so the pads sit on top of it. This used to be a plain
  // integer walk on the assumption that loops are written at the engine rate.
  // They are not: scripts/build-loop-packs.py renders every pack at 22050, so
  // once the engine moved to 32 kHz that assumption played every loop 45% fast
  // and silently broke the tempo lock. Loops now resample the same way pads do
  // (16.16 fixed point, 2-point linear), which also keeps every SD card that
  // is already in the field working untouched.
  if (loopPcm_ != nullptr && loopFrames_ > 0 && loopIncFP_ > 0) {
    uint64_t posFP = loopPosFP_;  // Q32.32
    const uint64_t endFP = (uint64_t)loopFrames_ << 32;
    int32_t gain = (int32_t)loopVolume_ * masterVolume_;  // 0..65025
    for (uint32_t n = 0; n < frames; n++) {
      uint32_t idx = (uint32_t)(posFP >> 32);
      // 16 bits of fraction is plenty for the interpolation itself; the extra
      // precision in posFP exists for the cycle length, not the crossfade.
      uint32_t frac = (uint32_t)((posFP >> 16) & 0xFFFF);
      int32_t s0 = loopPcm_[idx];
      // Wrap the interpolation partner too, so the seam between the last and
      // first frame is as smooth as every other sample boundary.
      int32_t s1 = loopPcm_[(idx + 1 >= loopFrames_) ? 0 : idx + 1];
      int32_t s = s0 + (((s1 - s0) * (int32_t)frac) >> 16);
      acc[n] += (s * gain) >> 16;
      posFP += loopIncFP_;
      if (posFP >= endFP) {
        posFP -= endFP;
      }
    }
    loopPosFP_ = posFP;
    loopPos_ = (uint32_t)(loopPosFP_ >> 32);  // UI reads this for the position bar
  }

  for (uint8_t i = 0; i <= kVoices; i++) {
    Voice &voice = voices_[i];
    if (!voice.active) {
      continue;
    }
    const int16_t *pcm = voice.pcm;
    for (uint32_t n = 0; n < frames; n++) {
      uint32_t idx = voice.posFP >> 16;
      if (idx + 1 >= voice.frames) {
        voice.active = false;
        break;
      }
      int32_t s0 = pcm[idx];
      int32_t s1 = pcm[idx + 1];
      int32_t frac = (int32_t)(voice.posFP & 0xFFFF);
      int32_t sample = s0 + (((s1 - s0) * frac) >> 16);
      int32_t gain = voice.gainQ12;
      if (voice.attack != 0) {
        gain = gain * (kAttackFrames - voice.attack) / kAttackFrames;
        voice.attack--;
      }
      acc[n] += (sample * gain) >> 12;
      voice.posFP += voice.incFP;
      if (voice.fadeDecQ12 != 0) {
        voice.gainQ12 -= voice.fadeDecQ12;
        if (voice.gainQ12 <= 0) {
          voice.active = false;
          break;
        }
      }
    }
  }
}

bool AudioEngine::post(const EngineCommand &cmd) {
  if (!running_) {
    return false;
  }
  uint16_t next = (uint16_t)((cmdHead_ + 1) & (kCmdRingSize - 1));
  if (next == cmdTail_) {
    return false;  // ring full; drop rather than block the UI
  }
  cmdRing_[cmdHead_] = cmd;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  cmdHead_ = next;
  return true;
}

bool AudioEngine::pushEvent_(const EngineEvent &evt) {
  uint16_t next = (uint16_t)((evtHead_ + 1) & (kEvtRingSize - 1));
  if (next == evtTail_) {
    return false;
  }
  evtRing_[evtHead_] = evt;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  evtHead_ = next;
  return true;
}

bool AudioEngine::nextEvent(EngineEvent &out) {
  if (evtTail_ == evtHead_) {
    return false;
  }
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  out = evtRing_[evtTail_];
  evtTail_ = (uint16_t)((evtTail_ + 1) & (kEvtRingSize - 1));
  return true;
}

SampleBank *AudioEngine::activeBank() {
  return banks_[activeBankIdx_] != nullptr ? banks_[activeBankIdx_] : banks_[0];
}

uint8_t AudioEngine::activeVoices() const {
  return activeVoiceCount_;
}

void AudioEngine::stageLoop(int16_t *pcm, uint32_t frames, uint64_t incFP) {
  loopPending_ = pcm;
  loopPendingFrames_ = frames;
  // 1<<32 reproduces the old plain integer walk exactly.
  loopPendingIncFP_ = incFP != 0 ? incFP : ((uint64_t)1 << 32);
}

int16_t *AudioEngine::takeRetiredLoop() {
  int16_t *old = loopRetired_;
  loopRetired_ = nullptr;
  return old;
}

uint16_t AudioEngine::copyScope(int16_t *out, uint16_t max) const {
  if (out == nullptr || max == 0) {
    return 0;
  }
  if (max > kScopeSize) {
    max = kScopeSize;
  }
  // Snapshot the write cursor once, then walk back `max` entries. The render
  // task may lap us mid-copy; that only re-colors part of one drawn frame.
  uint16_t w = scopeWrite_;
  for (uint16_t i = 0; i < max; i++) {
    out[i] = scope_[(uint16_t)(w - max + i) & (kScopeSize - 1)];
  }
  return max;
}

const char *AudioEngine::modeName() const {
  return "i2s-engine";
}

String AudioEngine::statusLine() const {
  return String("i2s ") + String(kRate) + "Hz block " + String(kBlockFrames) +
         "x" + String(CYPHER_TUNE_DMA_DESC) + " (" +
         String(kRingFrames * 1000 / kRate) + "ms ring) voices " +
         String(activeVoiceCount_) + "/" + String(kVoices) + " underruns " +
         String(underruns_) + " frames " + String(framesRendered_);
}

#else  // stub: no USE_AUDIO or no IDF i2s_std header

bool AudioEngine::begin(const HardwareProfile &profile, SampleBank *bankA,
                        SampleBank *bankB, Sequencer *seq, Stream &log) {
  (void)profile;
  banks_[0] = bankA;
  banks_[1] = bankB;
  seq_ = seq;
  log.println(F("[engine] stub: silent build (USE_AUDIO=0 or no i2s_std); "
                "sequencer runs on the millis clock"));
  return false;
}

bool AudioEngine::post(const EngineCommand &) { return false; }
bool AudioEngine::nextEvent(EngineEvent &) { return false; }

SampleBank *AudioEngine::activeBank() {
  return banks_[0];
}

uint8_t AudioEngine::activeVoices() const { return 0; }

// No mix to scope: returning 0 is the UI's cue to draw the simulated meters.
uint16_t AudioEngine::copyScope(int16_t *, uint16_t) const { return 0; }

// Silent build: accept the buffer and hand it straight back, so the loop
// context's alloc/free bookkeeping works identically with no engine running.
void AudioEngine::stageLoop(int16_t *pcm, uint32_t, uint64_t) { loopRetired_ = pcm; }

int16_t *AudioEngine::takeRetiredLoop() {
  int16_t *old = loopRetired_;
  loopRetired_ = nullptr;
  return old;
}

const char *AudioEngine::modeName() const {
  return "stub";
}

String AudioEngine::statusLine() const {
  return String("stub; no I2S output, millis clock drives the transport");
}

#endif

AudioEngine &audioEngine() {
  static AudioEngine engine;
  return engine;
}
