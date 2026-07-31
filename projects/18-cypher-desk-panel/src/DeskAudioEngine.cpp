#include "DeskAudioEngine.h"

#if CYPHER_DESK_AUDIO_BACKEND

#include <CrowPanelShared.h>  // HardwareProfile: audio pins and amp polarity
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {

// One render block per DMA descriptor. 256 frames at 44.1 kHz is 5.8 ms, so
// four descriptors queue ~23 ms - the same geometry projects 09 and 21 are
// hardware-verified on. The DMA ring only has to cover the mixer task's own
// scheduling jitter; the 1.5 s software ring is what absorbs a slow UI frame.
constexpr uint32_t kBlockFrames = 256;
constexpr uint32_t kDmaDesc = 4;
constexpr uint32_t kTaskStack = 4096;
constexpr uint32_t kTaskPrio = 10;
constexpr uint32_t kTaskCore = 0;  // leave core 1 for display, touch and SD

// Worst-case output frames produced from a single source frame, at the lowest
// source rate the WAV reader admits (8 kHz -> 44.1 kHz is 5.5). Checking for
// this much room before consuming a source frame means the converter never
// has to unwind a half-finished frame.
constexpr uint32_t kMaxOutputPerSource = (DeskAudioEngine::kOutputRate / 8000) + 2;

// Mixer scratch. One engine, one task, so file-scope internal SRAM is simpler
// than members and costs nothing in a silent build.
int32_t gAccLeft[kBlockFrames];
int32_t gAccRight[kBlockFrames];
int16_t gOut[kBlockFrames * 2];

int16_t clampSample(int32_t value) {
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return static_cast<int16_t>(value);
}

}  // namespace

bool DeskAudioEngine::begin(Print &log) {
  if (running_) return true;
#if !USE_CYPHER_DESK_AUDIO
  // Recorder-only build: the IDF driver is linked for the PDM microphone, but
  // there is no output path and the amp stays asleep.
  (void)log;
  status_ = "output disabled; microphone only";
  return false;
#else
  const HardwareProfile &profile = activeHardwareProfile();
  ampPin_ = profile.audio.control;
  ampActiveHigh_ = profile.audio.controlActiveHigh;

  // Park the amp asleep before the bus exists. The level comes from the
  // profile - IO30 is ACTIVE-LOW on this panel and driving it HIGH mutes the
  // speaker while I2S happily keeps streaming, which is the silent-but-
  // "working" failure this repo already paid for once.
  pinMode(ampPin_, OUTPUT);
  setAmp(false);

  ringFrames_ = CYPHER_DESK_AUDIO_RING_FRAMES;
  ringMask_ = ringFrames_ - 1;
  if ((ringFrames_ & ringMask_) != 0) {
    status_ = "ring size is not a power of two";
    log.println(F("[desk-audio] CYPHER_DESK_AUDIO_RING_FRAMES must be a power of two"));
    return false;
  }
  const size_t ringBytes = static_cast<size_t>(ringFrames_) * 2 * sizeof(int16_t);
  ring_ = static_cast<int16_t *>(heap_caps_malloc(ringBytes, MALLOC_CAP_SPIRAM));
  if (ring_ == nullptr) ring_ = static_cast<int16_t *>(malloc(ringBytes));
  if (ring_ == nullptr) {
    status_ = "audio ring allocation failed";
    log.println(F("[desk-audio] ring allocation failed"));
    return false;
  }
  memset(ring_, 0, ringBytes);

  i2s_chan_config_t channelConfig = {};
  channelConfig.id = I2S_NUM_AUTO;
  channelConfig.role = I2S_ROLE_MASTER;
  channelConfig.dma_desc_num = kDmaDesc;
  channelConfig.dma_frame_num = kBlockFrames;
  channelConfig.auto_clear = true;  // underrun emits silence, not stale audio
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&channelConfig, &tx, nullptr) != ESP_OK) {
    status_ = "i2s channel allocation failed";
    log.println(F("[desk-audio] i2s_new_channel failed"));
    return false;
  }

  // Philips slot, 16-bit stereo, no MCLK - the NS4168 derives everything from
  // BCLK/LRCLK and needs no codec or I2C setup.
  i2s_std_config_t standardConfig = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kOutputRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = static_cast<gpio_num_t>(profile.audio.bclk),
          .ws = static_cast<gpio_num_t>(profile.audio.lrclk),
          .dout = static_cast<gpio_num_t>(profile.audio.sdata),
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {false, false, false},
      },
  };
  if (i2s_channel_init_std_mode(tx, &standardConfig) != ESP_OK ||
      i2s_channel_enable(tx) != ESP_OK) {
    status_ = "i2s init failed; silent";
    log.println(F("[desk-audio] i2s std init/enable failed"));
    i2s_del_channel(tx);
    return false;
  }
  txChannel_ = tx;

  // Stream silence before waking the amp so it comes up on a clean bus instead
  // of a pop.
  memset(gOut, 0, sizeof(gOut));
  for (uint8_t block = 0; block < 4; ++block) {
    size_t written = 0;
    i2s_channel_write(tx, gOut, sizeof(gOut), &written, pdMS_TO_TICKS(50));
  }
  setAmp(true);

  running_ = true;
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(mixerTrampoline, "deskmix", kTaskStack, this, kTaskPrio, &task,
                              kTaskCore) != pdPASS) {
    running_ = false;
    setAmp(false);
    status_ = "mixer task create failed";
    log.println(F("[desk-audio] mixer task create failed"));
    return false;
  }
  task_ = task;

  status_ = String("ready ") + kOutputRate + " Hz stereo, ring " +
            (ringFrames_ * 1000UL / kOutputRate) + " ms";
  log.println(String("[desk-audio] ") + status_);
  return true;
#endif  // USE_CYPHER_DESK_AUDIO
}

void DeskAudioEngine::end() {
  if (!running_) return;
  running_ = false;
  streamOpen_ = false;
  if (task_ != nullptr) {
    vTaskDelete(static_cast<TaskHandle_t>(task_));
    task_ = nullptr;
  }
  setAmp(false);
  if (txChannel_ != nullptr) {
    i2s_channel_disable(static_cast<i2s_chan_handle_t>(txChannel_));
    i2s_del_channel(static_cast<i2s_chan_handle_t>(txChannel_));
    txChannel_ = nullptr;
  }
  endMicrophone();
  status_ = "audio stopped";
}

bool DeskAudioEngine::ready() const { return running_; }
String DeskAudioEngine::status() const { return status_; }

void DeskAudioEngine::setAmp(bool on) {
  digitalWrite(ampPin_, on == ampActiveHigh_ ? HIGH : LOW);
}

// --- Streaming voice -------------------------------------------------------

bool DeskAudioEngine::openStream(uint32_t sourceRate, uint16_t channels, uint16_t bits) {
  if (!running_) return false;
  if (sourceRate == 0 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16)) {
    return false;
  }
  closeStream();
  sourceRate_ = sourceRate;
  sourceChannels_ = channels;
  sourceBits_ = bits;
  // Q16 source frames consumed per output frame. Upsampling gives a step below
  // 1.0, so one source frame produces several output frames.
  phaseStep_ = static_cast<uint32_t>((static_cast<uint64_t>(sourceRate) << 16) / kOutputRate);
  phase_ = 0;
  prevLeft_ = 0;
  prevRight_ = 0;
  havePrev_ = false;
  partialLength_ = 0;
  streamPlayed_ = 0;
  underruns_ = 0;
  streamEnding_ = false;
  streamOpen_ = true;
  return true;
}

bool DeskAudioEngine::emitFrame(int16_t left, int16_t right) {
  const uint32_t used = ringWrite_ - ringRead_;
  if (used >= ringFrames_) return false;
  const uint32_t slot = (ringWrite_ & ringMask_) * 2;
  ring_[slot] = left;
  ring_[slot + 1] = right;
  // Publish the samples before the index that makes them visible.
  __atomic_thread_fence(__ATOMIC_RELEASE);
  ringWrite_ = ringWrite_ + 1;
  return true;
}

size_t DeskAudioEngine::pushStream(const uint8_t *bytes, size_t length) {
  if (!running_ || !streamOpen_ || bytes == nullptr) return 0;
  const uint32_t sourceFrameBytes = sourceChannels_ * (sourceBits_ / 8);
  size_t consumed = 0;

  while (consumed < length) {
    // Refuse to start a source frame unless the ring can hold every output
    // frame it might produce. That is what keeps the converter stateless
    // between calls apart from phase and the previous frame.
    const uint32_t free = ringFrames_ - (ringWrite_ - ringRead_);
    if (free < kMaxOutputPerSource) break;

    // Reassemble a source frame that straddled the end of the last push.
    uint8_t frameBytes[4] = {};
    if (partialLength_ > 0) {
      const uint32_t need = sourceFrameBytes - partialLength_;
      if (length - consumed < need) {
        memcpy(partial_ + partialLength_, bytes + consumed, length - consumed);
        partialLength_ += static_cast<uint8_t>(length - consumed);
        consumed = length;
        break;
      }
      memcpy(frameBytes, partial_, partialLength_);
      memcpy(frameBytes + partialLength_, bytes + consumed, need);
      consumed += need;
      partialLength_ = 0;
    } else {
      if (length - consumed < sourceFrameBytes) {
        partialLength_ = static_cast<uint8_t>(length - consumed);
        memcpy(partial_, bytes + consumed, partialLength_);
        consumed = length;
        break;
      }
      memcpy(frameBytes, bytes + consumed, sourceFrameBytes);
      consumed += sourceFrameBytes;
    }

    int16_t left = 0;
    int16_t right = 0;
    if (sourceBits_ == 16) {
      left = static_cast<int16_t>(frameBytes[0] | (frameBytes[1] << 8));
      right = sourceChannels_ == 2
                  ? static_cast<int16_t>(frameBytes[2] | (frameBytes[3] << 8))
                  : left;
    } else {
      // 8-bit WAV samples are unsigned, centred on 128.
      left = static_cast<int16_t>((static_cast<int16_t>(frameBytes[0]) - 128) << 8);
      right = sourceChannels_ == 2
                  ? static_cast<int16_t>((static_cast<int16_t>(frameBytes[1]) - 128) << 8)
                  : left;
    }

    if (!havePrev_) {
      prevLeft_ = left;
      prevRight_ = right;
      havePrev_ = true;
      continue;
    }

    // Linear interpolation between the previous source frame and this one.
    // phase_ walks from 0 to 1.0 (Q16) across the gap, emitting one output
    // frame per step.
    while (phase_ < 0x10000u) {
      const int32_t weight = static_cast<int32_t>(phase_);
      const int32_t outLeft =
          prevLeft_ + (((static_cast<int32_t>(left) - prevLeft_) * weight) >> 16);
      const int32_t outRight =
          prevRight_ + (((static_cast<int32_t>(right) - prevRight_) * weight) >> 16);
      if (!emitFrame(static_cast<int16_t>(outLeft), static_cast<int16_t>(outRight))) break;
      phase_ += phaseStep_;
    }
    phase_ = phase_ >= 0x10000u ? phase_ - 0x10000u : phase_;
    prevLeft_ = left;
    prevRight_ = right;
  }
  return consumed;
}

uint32_t DeskAudioEngine::streamFreeSourceFrames() const {
  if (!running_ || !streamOpen_ || phaseStep_ == 0) return 0;
  const uint32_t free = ringFrames_ - (ringWrite_ - ringRead_);
  if (free < kMaxOutputPerSource) return 0;
  // free output frames * (sourceRate / outputRate), floored.
  return static_cast<uint32_t>((static_cast<uint64_t>(free) * phaseStep_) >> 16);
}

uint32_t DeskAudioEngine::streamQueuedFrames() const { return ringWrite_ - ringRead_; }

void DeskAudioEngine::closeStream() {
  streamOpen_ = false;
  streamEnding_ = false;
  // The mixer only reads up to ringWrite_, so moving the read index forward is
  // enough to drop the queue without touching the buffer.
  ringRead_ = ringWrite_;
  partialLength_ = 0;
  havePrev_ = false;
  phase_ = 0;
}

void DeskAudioEngine::endStreamInput() {
  if (streamOpen_) streamEnding_ = true;
}

bool DeskAudioEngine::streamActive() const {
  return streamOpen_ && (!streamEnding_ || streamQueuedFrames() > 0);
}

uint64_t DeskAudioEngine::streamPlayedFrames() const { return streamPlayed_; }
uint32_t DeskAudioEngine::streamUnderruns() const { return underruns_; }

// --- One-shots -------------------------------------------------------------

void DeskAudioEngine::playClip(const int16_t *pcm, uint32_t frames, uint8_t gainPercent) {
  if (!running_ || pcm == nullptr || frames == 0) return;
  // Round-robin with steal. Called from the touch path, so it does no float
  // math and never blocks.
  uint8_t slot = nextShot_;
  for (uint8_t attempt = 0; attempt < kOneShotVoices; ++attempt) {
    const uint8_t candidate = (nextShot_ + attempt) % kOneShotVoices;
    if (!shots_[candidate].active) {
      slot = candidate;
      break;
    }
    if (attempt + 1 == kOneShotVoices) slot = nextShot_;  // all busy: steal
  }
  nextShot_ = (slot + 1) % kOneShotVoices;

  OneShot &shot = shots_[slot];
  shot.active = false;  // stop the mixer reading a half-updated voice
  __atomic_thread_fence(__ATOMIC_RELEASE);
  shot.pcm = pcm;
  shot.frames = frames;
  shot.position = 0;
  shot.gainQ12 = static_cast<uint16_t>((gainPercent > 100 ? 100 : gainPercent) * 4096 / 100);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  shot.active = true;
}

void DeskAudioEngine::stopClips() {
  for (uint8_t i = 0; i < kOneShotVoices; ++i) shots_[i].active = false;
}

void DeskAudioEngine::setVolume(uint8_t percent) { volume_ = percent > 100 ? 100 : percent; }
uint8_t DeskAudioEngine::volume() const { return volume_; }

// --- Mixer task ------------------------------------------------------------

void DeskAudioEngine::mixerTrampoline(void *self) {
  static_cast<DeskAudioEngine *>(self)->mixerTask();
}

void DeskAudioEngine::mixerTask() {
  i2s_chan_handle_t tx = static_cast<i2s_chan_handle_t>(txChannel_);
  for (;;) {
    memset(gAccLeft, 0, sizeof(gAccLeft));
    memset(gAccRight, 0, sizeof(gAccRight));

    // Streaming voice.
    const uint32_t queued = ringWrite_ - ringRead_;
    uint32_t take = queued < kBlockFrames ? queued : kBlockFrames;
    if (streamOpen_) {
      if (take < kBlockFrames && !streamEnding_) ++underruns_;
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      const uint16_t gainQ12 = static_cast<uint16_t>(volume_ * 4096 / 100);
      for (uint32_t n = 0; n < take; ++n) {
        const uint32_t slot = ((ringRead_ + n) & ringMask_) * 2;
        gAccLeft[n] += (static_cast<int32_t>(ring_[slot]) * gainQ12) >> 12;
        gAccRight[n] += (static_cast<int32_t>(ring_[slot + 1]) * gainQ12) >> 12;
      }
      ringRead_ = ringRead_ + take;
      streamPlayed_ = streamPlayed_ + take;
      if (streamEnding_ && (ringWrite_ - ringRead_) == 0) {
        streamOpen_ = false;
        streamEnding_ = false;
      }
    } else {
      take = 0;
    }

    // One-shot voices, mono, summed to both channels.
    for (uint8_t i = 0; i < kOneShotVoices; ++i) {
      OneShot &shot = shots_[i];
      if (!shot.active) continue;
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      const int16_t *pcm = shot.pcm;
      uint32_t n = 0;
      while (n < kBlockFrames && shot.position < shot.frames) {
        const int32_t sample = (static_cast<int32_t>(pcm[shot.position]) * shot.gainQ12) >> 12;
        gAccLeft[n] += sample;
        gAccRight[n] += sample;
        ++shot.position;
        ++n;
      }
      if (shot.position >= shot.frames) shot.active = false;
    }

    // Clamp rather than duck. Clicks over music are short transients, so hard
    // limiting is inaudible where an automatic gain dip would pump.
    for (uint32_t n = 0; n < kBlockFrames; ++n) {
      gOut[2 * n] = clampSample(gAccLeft[n]);
      gOut[2 * n + 1] = clampSample(gAccRight[n]);
    }

    // The blocking write paces this task. Silence between tracks keeps the DMA
    // fed, which is also what keeps the idle amp quiet.
    size_t written = 0;
    i2s_channel_write(tx, gOut, sizeof(gOut), &written, portMAX_DELAY);
  }
}

// --- Microphone ------------------------------------------------------------

bool DeskAudioEngine::beginMicrophone() {
#if USE_CYPHER_DESK_RECORDER
  if (micReady_) return true;
  const HardwareProfile &profile = activeHardwareProfile();
  i2s_chan_config_t channelConfig =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  i2s_chan_handle_t rx = nullptr;
  if (i2s_new_channel(&channelConfig, nullptr, &rx) != ESP_OK) return false;

  i2s_pdm_rx_config_t pdmConfig = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(CYPHER_DESK_AUDIO_MIC_RATE),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                 I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .clk = static_cast<gpio_num_t>(profile.audio.micClk),
          .din = static_cast<gpio_num_t>(profile.audio.micDin),
          .invert_flags = {false},
      },
  };
  if (i2s_channel_init_pdm_rx_mode(rx, &pdmConfig) != ESP_OK ||
      i2s_channel_enable(rx) != ESP_OK) {
    i2s_del_channel(rx);
    return false;
  }
  rxChannel_ = rx;
  micReady_ = true;
  return true;
#else
  return false;
#endif
}

void DeskAudioEngine::endMicrophone() {
  if (rxChannel_ == nullptr) return;
  i2s_channel_disable(static_cast<i2s_chan_handle_t>(rxChannel_));
  i2s_del_channel(static_cast<i2s_chan_handle_t>(rxChannel_));
  rxChannel_ = nullptr;
  micReady_ = false;
}

bool DeskAudioEngine::microphoneReady() const { return micReady_; }

size_t DeskAudioEngine::readMicrophone(uint8_t *destination, size_t length) {
  if (!micReady_ || destination == nullptr) return 0;
  size_t read = 0;
  // Short timeout: the recorder is serviced from loop context and must not
  // stall the UI waiting on the microphone.
  if (i2s_channel_read(static_cast<i2s_chan_handle_t>(rxChannel_), destination, length, &read,
                       pdMS_TO_TICKS(10)) != ESP_OK) {
    return 0;
  }
  return read;
}

#else  // CYPHER_DESK_AUDIO_BACKEND

// Silent build. Every caller compiles unchanged and simply never hears
// anything - the same shape projects 09/20/21/22 use so no call site needs an
// #ifdef around it.

bool DeskAudioEngine::begin(Print &log) {
  (void)log;
  status_ = USE_CYPHER_DESK_AUDIO ? "audio driver unavailable; silent" : "audio disabled";
  return false;
}
void DeskAudioEngine::end() {}
bool DeskAudioEngine::ready() const { return false; }
String DeskAudioEngine::status() const { return status_; }
bool DeskAudioEngine::openStream(uint32_t, uint16_t, uint16_t) { return false; }
size_t DeskAudioEngine::pushStream(const uint8_t *, size_t) { return 0; }
uint32_t DeskAudioEngine::streamFreeSourceFrames() const { return 0; }
uint32_t DeskAudioEngine::streamQueuedFrames() const { return 0; }
void DeskAudioEngine::closeStream() {}
void DeskAudioEngine::endStreamInput() {}
bool DeskAudioEngine::streamActive() const { return false; }
uint64_t DeskAudioEngine::streamPlayedFrames() const { return 0; }
uint32_t DeskAudioEngine::streamUnderruns() const { return 0; }
void DeskAudioEngine::playClip(const int16_t *, uint32_t, uint8_t) {}
void DeskAudioEngine::stopClips() {}
void DeskAudioEngine::setVolume(uint8_t percent) { volume_ = percent > 100 ? 100 : percent; }
uint8_t DeskAudioEngine::volume() const { return volume_; }
bool DeskAudioEngine::beginMicrophone() { return false; }
void DeskAudioEngine::endMicrophone() {}
bool DeskAudioEngine::microphoneReady() const { return false; }
size_t DeskAudioEngine::readMicrophone(uint8_t *, size_t) { return 0; }

#endif  // CYPHER_DESK_AUDIO_BACKEND
