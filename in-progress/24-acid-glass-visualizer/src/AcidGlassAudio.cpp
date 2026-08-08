#include "AcidGlassAudio.h"
#include "AcidGlassLogic.h"

#include <CrowPanelShared.h>
#include <math.h>
#include <string.h>

#if USE_ACID_GLASS_SD
#include <SD_MMC.h>
#endif

#if USE_ACID_GLASS_AUDIO && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <driver/i2s_std.h>
#endif

namespace {

uint16_t gChannels = 0;
bool gHavePair = false;

#if USE_ACID_GLASS_SD
File gTrack;
uint8_t gReadBuffer[4096];
uint16_t gReadPosition = 0;
uint16_t gReadLength = 0;
uint32_t gDataRemaining = 0;
uint32_t gSourceRate = 0;
uint16_t gBits = 0;
uint32_t gPhase = 0;
uint32_t gPhaseStep = 0;
int16_t gLeftA = 0, gRightA = 0, gLeftB = 0, gRightB = 0;

uint16_t readU16(File &file) {
  uint8_t b[2] = {};
  return file.read(b, 2) == 2 ? static_cast<uint16_t>(b[0] | (b[1] << 8)) : 0;
}

uint32_t readU32(File &file) {
  uint8_t b[4] = {};
  return file.read(b, 4) == 4
             ? static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24))
             : 0;
}

bool wavSuffix(const char *name) {
  String lower(name != nullptr ? name : "");
  lower.toLowerCase();
  return lower.endsWith(".wav");
}
#endif

constexpr uint32_t kOutputRate = 44100;
constexpr uint8_t kDmaDescriptors = 6;
const float kBandHz[kAcidBandCount] = {70, 140, 280, 560, 1100, 2200, 4400, 8800};

int16_t triangleWave(uint32_t phase) {
  uint32_t position = phase >> 16;
  int32_t value = position < 32768 ? static_cast<int32_t>(position) * 2 - 32768
                                   : 98303 - static_cast<int32_t>(position) * 2;
  return static_cast<int16_t>(value);
}

}  // namespace

bool AcidGlassAudio::mountAndIndex() {
#if USE_ACID_GLASS_SD
  trackCount_ = 0;
  bool mounted = SD_MMC.cardType() != CARD_NONE;
  if (!mounted) mounted = SD_MMC.begin("/sdcard", ACID_GLASS_SDMMC_1BIT != 0);
  sdReady_ = mounted && SD_MMC.cardType() != CARD_NONE;
  if (!sdReady_) {
    snprintf(status_, sizeof(status_), "SD unavailable; demo mode only");
    return false;
  }

  File directory = SD_MMC.open(ACID_GLASS_MUSIC_DIR);
  if (!directory || !directory.isDirectory()) {
    SD_MMC.mkdir("/acid-glass");
    SD_MMC.mkdir(ACID_GLASS_MUSIC_DIR);
    snprintf(status_, sizeof(status_), "SD ready; created %s; internal beat available",
             ACID_GLASS_MUSIC_DIR);
    return true;
  }
  File entry = directory.openNextFile();
  while (entry && trackCount_ < ACID_GLASS_MAX_TRACKS) {
    if (!entry.isDirectory() && wavSuffix(entry.name())) {
      int length = snprintf(tracks_[trackCount_], kPathLength, "%s/%s",
                            ACID_GLASS_MUSIC_DIR, entry.name());
      if (length > 0 && length < kPathLength) trackCount_++;
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  snprintf(status_, sizeof(status_), "SD ready; %u WAV track%s%s", trackCount_,
           trackCount_ == 1 ? "" : "s", trackCount_ == 0 ? "; internal beat available" : "");
  return true;
#else
  snprintf(status_, sizeof(status_), "SD disabled; demo mode only");
  return false;
#endif
}

bool AcidGlassAudio::begin() {
#if USE_ACID_GLASS_AUDIO && defined(CONFIG_IDF_TARGET_ESP32P4)
  const AudioPins &pins = activeHardwareProfile().audio;
  pinMode(pins.control, OUTPUT);
  digitalWrite(pins.control, pins.controlActiveHigh ? LOW : HIGH);

  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = kDmaDescriptors;
  channelConfig.dma_frame_num = kBlockFrames;
  channelConfig.auto_clear = true;
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&channelConfig, &tx, nullptr) != ESP_OK) {
    snprintf(status_, sizeof(status_), "I2S channel allocation failed");
    return false;
  }
  i2s_std_config_t standardConfig = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kOutputRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = static_cast<gpio_num_t>(pins.bclk),
          .ws = static_cast<gpio_num_t>(pins.lrclk),
          .dout = static_cast<gpio_num_t>(pins.sdata),
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {false, false, false},
      },
  };
  if (i2s_channel_init_std_mode(tx, &standardConfig) != ESP_OK ||
      i2s_channel_enable(tx) != ESP_OK) {
    i2s_del_channel(tx);
    snprintf(status_, sizeof(status_), "I2S initialization failed");
    return false;
  }
  int16_t silence[kBlockFrames * 2] = {};
  for (uint8_t i = 0; i < 4; ++i) {
    size_t written = 0;
    i2s_channel_write(tx, silence, sizeof(silence), &written, pdMS_TO_TICKS(50));
  }
  digitalWrite(pins.control, pins.controlActiveHigh ? HIGH : LOW);
  txChannel_ = tx;
  audioReady_ = true;

  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(taskEntry_, "acid-audio", 6144, this, 10, &task, 0) != pdPASS) {
    audioReady_ = false;
    digitalWrite(pins.control, pins.controlActiveHigh ? LOW : HIGH);
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    txChannel_ = nullptr;
    snprintf(status_, sizeof(status_), "audio task creation failed");
    return false;
  }
  task_ = task;
  snprintf(status_, sizeof(status_), "44.1 kHz output ready; %u WAV; internal beat %s",
           trackCount_, trackCount_ == 0 ? "on PLAY" : "standby");
  return true;
#else
  snprintf(status_, sizeof(status_), "audio disabled; demo analyzer active");
  return false;
#endif
}

void AcidGlassAudio::end() {
#if USE_ACID_GLASS_AUDIO && defined(CONFIG_IDF_TARGET_ESP32P4)
  audioReady_ = false;
  if (task_ != nullptr) {
    vTaskDelete(static_cast<TaskHandle_t>(task_));
    task_ = nullptr;
  }
  closeTrack_();
  if (txChannel_ != nullptr) {
    i2s_channel_disable(static_cast<i2s_chan_handle_t>(txChannel_));
    i2s_del_channel(static_cast<i2s_chan_handle_t>(txChannel_));
    txChannel_ = nullptr;
  }
#endif
}

bool AcidGlassAudio::play(uint8_t index) {
  if (!audioReady_) return false;
  if (trackCount_ == 0) {
    requestedTrack_ = -1;
    stopRequested_ = false;
    internalSynth_ = true;
    playing_ = true;
    snprintf(status_, sizeof(status_), "playing INTERNAL ACID BEAT");
    return true;
  }
  if (index >= trackCount_) return false;
  internalSynth_ = false;
  requestedTrack_ = index;
  stopRequested_ = false;
  return true;
}

void AcidGlassAudio::stop() { stopRequested_ = true; }

void AcidGlassAudio::next() {
  if (trackCount_ > 0) play((activeTrack_ + 1) % trackCount_);
}

void AcidGlassAudio::previous() {
  if (trackCount_ > 0) play((activeTrack_ + trackCount_ - 1) % trackCount_);
}

void AcidGlassAudio::setVolume(uint8_t volume) { volume_ = min<uint8_t>(volume, 100); }
void AcidGlassAudio::setSensitivity(uint8_t sensitivity) { sensitivity_ = sensitivity; }

const char *AcidGlassAudio::trackName(uint8_t index) const {
  if (trackCount_ == 0) return "INTERNAL ACID BEAT";
  if (index >= trackCount_) return "NO TRACK";
  const char *slash = strrchr(tracks_[index], '/');
  return slash != nullptr ? slash + 1 : tracks_[index];
}

AudioFeatures AcidGlassAudio::features() const {
  AudioFeatures copy;
  portENTER_CRITICAL(&featureMux_);
  copy = features_;
  portEXIT_CRITICAL(&featureMux_);
  return copy;
}

void AcidGlassAudio::synthFeatures(uint32_t nowMs, AudioFeatures &out) const {
  const float beat = fmodf(nowMs / 500.0f, 1.0f);
  const float pulse = expf(-beat * 7.0f);
  out.peak = static_cast<uint8_t>(80 + pulse * 175);
  out.rms = static_cast<uint8_t>(55 + pulse * 150);
  out.onset = beat < 0.08f ? 255 : 0;
  for (uint8_t i = 0; i < kAcidBandCount; ++i) {
    float wave = 0.5f + 0.5f * sinf(nowMs * (0.0018f + i * 0.00017f) + i * 0.73f);
    out.bands[i] = static_cast<uint8_t>((35 + wave * 150) + pulse * (i < 3 ? 70 : 24));
  }
  out.sequence++;
}

void AcidGlassAudio::taskEntry_(void *self) {
  static_cast<AcidGlassAudio *>(self)->taskLoop_();
}

void AcidGlassAudio::taskLoop_() {
#if USE_ACID_GLASS_AUDIO && defined(CONFIG_IDF_TARGET_ESP32P4)
  int16_t output[kBlockFrames * 2] = {};
  int16_t mono[kBlockFrames] = {};
  while (audioReady_) {
    if (stopRequested_) {
      closeTrack_();
      stopRequested_ = false;
    }
    int16_t request = requestedTrack_;
    if (request >= 0) {
      requestedTrack_ = -1;
      openTrack_(static_cast<uint8_t>(request));
    }
    if (!playing_) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    memset(output, 0, sizeof(output));
    memset(mono, 0, sizeof(mono));

    if (internalSynth_) {
      renderInternalSynth_(output, mono, kBlockFrames);
      analyze_(mono, kBlockFrames);
      size_t written = 0;
      esp_err_t result = i2s_channel_write(static_cast<i2s_chan_handle_t>(txChannel_), output,
                                           sizeof(output), &written, pdMS_TO_TICKS(100));
      if (result != ESP_OK || written != sizeof(output)) underruns_++;
      continue;
    }

    bool complete = false;
    for (uint16_t i = 0; i < kBlockFrames; ++i) {
      if (!gHavePair) {
        if (!readSourceFrame_(gLeftA, gRightA) || !readSourceFrame_(gLeftB, gRightB)) {
          complete = true;
          break;
        }
        gHavePair = true;
      }
      int32_t left = gLeftA + (((int32_t)gLeftB - gLeftA) * (int32_t)gPhase >> 16);
      int32_t right = gRightA + (((int32_t)gRightB - gRightA) * (int32_t)gPhase >> 16);
      gPhase += gPhaseStep;
      while (gPhase >= 0x10000UL) {
        gPhase -= 0x10000UL;
        gLeftA = gLeftB;
        gRightA = gRightB;
        if (!readSourceFrame_(gLeftB, gRightB)) { complete = true; break; }
      }
      int32_t mixed = (left + right) / 2;
      mono[i] = static_cast<int16_t>(mixed);
      output[i * 2] = static_cast<int16_t>(left * volume_ / 100);
      output[i * 2 + 1] = static_cast<int16_t>(right * volume_ / 100);
      if (complete) break;
    }
    analyze_(mono, kBlockFrames);
    size_t written = 0;
    esp_err_t result = i2s_channel_write(static_cast<i2s_chan_handle_t>(txChannel_), output,
                                         sizeof(output), &written, pdMS_TO_TICKS(100));
    if (result != ESP_OK || written != sizeof(output)) underruns_++;
    if (complete) {
      uint8_t nextTrack = trackCount_ > 0 ? (activeTrack_ + 1) % trackCount_ : 0;
      if (!openTrack_(nextTrack)) closeTrack_();
    }
  }
  vTaskDelete(nullptr);
#endif
}

bool AcidGlassAudio::openTrack_(uint8_t index) {
#if USE_ACID_GLASS_SD && USE_ACID_GLASS_AUDIO
  closeTrack_();
  if (index >= trackCount_) return false;
  gTrack = SD_MMC.open(tracks_[index], FILE_READ);
  if (!gTrack) {
    snprintf(status_, sizeof(status_), "could not open %s", trackName(index));
    return false;
  }
  char riff[4] = {}, wave[4] = {};
  if (gTrack.readBytes(riff, 4) != 4 || readU32(gTrack) == 0 ||
      gTrack.readBytes(wave, 4) != 4 || memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) {
    closeTrack_();
    snprintf(status_, sizeof(status_), "invalid RIFF/WAVE header");
    return false;
  }
  bool formatOk = false;
  bool dataFound = false;
  while (gTrack.available()) {
    char id[4] = {};
    if (gTrack.readBytes(id, 4) != 4) break;
    uint32_t size = readU32(gTrack);
    if (!memcmp(id, "fmt ", 4)) {
      if (size < 16) {
        closeTrack_();
        snprintf(status_, sizeof(status_), "invalid fmt chunk");
        return false;
      }
      uint16_t format = readU16(gTrack);
      gChannels = readU16(gTrack);
      gSourceRate = readU32(gTrack);
      gTrack.seek(gTrack.position() + 6);
      gBits = readU16(gTrack);
      if (size > 16) gTrack.seek(gTrack.position() + size - 16 + (size & 1));
      formatOk = AcidGlassLogic::validPcmFormat(format, gChannels, gBits, gSourceRate);
    } else if (!memcmp(id, "data", 4) && formatOk) {
      gDataRemaining = size;
      dataFound = true;
      break;
    } else {
      gTrack.seek(gTrack.position() + size + (size & 1));
    }
  }
  if (!dataFound) {
    closeTrack_();
    snprintf(status_, sizeof(status_), "need PCM16 mono/stereo 8-48 kHz");
    return false;
  }
  gReadPosition = gReadLength = 0;
  gPhase = 0;
  gPhaseStep = AcidGlassLogic::resampleStepQ16(gSourceRate, kOutputRate);
  gHavePair = false;
  internalSynth_ = false;
  activeTrack_ = index;
  playing_ = true;
  snprintf(status_, sizeof(status_), "playing %s", trackName(index));
  return true;
#else
  (void)index;
  return false;
#endif
}

void AcidGlassAudio::closeTrack_() {
#if USE_ACID_GLASS_SD
  if (gTrack) gTrack.close();
#endif
  playing_ = false;
  internalSynth_ = false;
  gHavePair = false;
}

void AcidGlassAudio::renderInternalSynth_(int16_t *output, int16_t *mono, uint16_t frames) {
  constexpr uint32_t kBpm = 118;
  constexpr uint32_t kSamplesPerBeat = kOutputRate * 60UL / kBpm;
  constexpr uint32_t kSamplesPerHalfBeat = kSamplesPerBeat / 2;
  const uint16_t bassNotes[4] = {55, 65, 73, 49};
  for (uint16_t i = 0; i < frames; ++i) {
    uint32_t beatPosition = synthSampleClock_ % kSamplesPerBeat;
    uint32_t beat = (synthSampleClock_ / kSamplesPerBeat) & 3U;
    uint32_t kickEnvelope = beatPosition < 3600 ? 3600 - beatPosition : 0;
    uint32_t kickHz = 46 + kickEnvelope / 42;
    synthKickPhase_ += static_cast<uint32_t>((static_cast<uint64_t>(kickHz) << 32) / kOutputRate);
    int32_t kick = static_cast<int32_t>(triangleWave(synthKickPhase_)) * kickEnvelope / 4200;

    uint32_t bassStep = static_cast<uint32_t>(
        (static_cast<uint64_t>(bassNotes[beat]) << 32) / kOutputRate);
    synthBassPhase_ += bassStep;
    int32_t bass = static_cast<int32_t>(triangleWave(synthBassPhase_)) * 7 / 20;
    if (beatPosition < 2800) bass = bass * beatPosition / 2800;

    uint32_t hatPosition = synthSampleClock_ % kSamplesPerHalfBeat;
    synthNoise_ ^= synthNoise_ << 13;
    synthNoise_ ^= synthNoise_ >> 17;
    synthNoise_ ^= synthNoise_ << 5;
    int32_t hat = 0;
    if (hatPosition < 760) {
      int32_t noise = static_cast<int16_t>(synthNoise_ >> 16);
      hat = noise * static_cast<int32_t>(760 - hatPosition) / 4200;
    }

    int32_t mixed = constrain(kick + bass + hat, -32768L, 32767L);
    mono[i] = static_cast<int16_t>(mixed);
    int16_t scaled = static_cast<int16_t>(mixed * volume_ / 100);
    output[i * 2] = scaled;
    output[i * 2 + 1] = scaled;
    synthSampleClock_++;
  }
}

int AcidGlassAudio::readByte_() {
#if USE_ACID_GLASS_SD
  if (gDataRemaining == 0) return -1;
  if (gReadPosition >= gReadLength) {
    uint16_t wanted = min<uint32_t>(kReadBufferBytes, gDataRemaining);
    gReadLength = gTrack.read(gReadBuffer, wanted);
    gReadPosition = 0;
    if (gReadLength == 0) return -1;
  }
  gDataRemaining--;
  return gReadBuffer[gReadPosition++];
#else
  return -1;
#endif
}

bool AcidGlassAudio::readSourceFrame_(int16_t &left, int16_t &right) {
  int lo = readByte_(), hi = readByte_();
  if (lo < 0 || hi < 0) return false;
  left = static_cast<int16_t>(lo | (hi << 8));
  if (gChannels == 2) {
    lo = readByte_();
    hi = readByte_();
    if (lo < 0 || hi < 0) return false;
    right = static_cast<int16_t>(lo | (hi << 8));
  } else {
    right = left;
  }
  return true;
}

void AcidGlassAudio::analyze_(const int16_t *mono, uint16_t frames) {
  uint32_t peak = 0;
  uint64_t squares = 0;
  for (uint16_t i = 0; i < frames; ++i) {
    int32_t sample = mono[i];
    uint32_t magnitude = abs(sample);
    if (magnitude > peak) peak = magnitude;
    squares += static_cast<uint64_t>(sample) * sample;
  }
  AudioFeatures next;
  next.peak = min<uint32_t>(255, peak * sensitivity_ / 32767);
  next.rms = min<uint32_t>(255, sqrtf(static_cast<float>(squares / frames)) * sensitivity_ / 32767);
  for (uint8_t band = 0; band < kAcidBandCount; ++band) {
    const float omega = TWO_PI * kBandHz[band] / kOutputRate;
    const float coefficient = 2.0f * cosf(omega);
    float s0 = 0, s1 = 0, s2 = 0;
    for (uint16_t i = 0; i < frames; ++i) {
      float window = 0.5f - 0.5f * cosf(TWO_PI * i / (frames - 1));
      s0 = mono[i] * window + coefficient * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    float power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    float level = sqrtf(max(0.0f, power)) / (frames * 100.0f);
    next.bands[band] = clampByte(static_cast<int32_t>(level * sensitivity_ / 128));
  }
  AudioFeatures previous = features();
  int16_t delta = static_cast<int16_t>(next.rms) - previous.rms;
  next.onset = delta > 18 ? clampByte(delta * 8) : 0;
  next.sequence = previous.sequence + 1;
  portENTER_CRITICAL(&featureMux_);
  features_ = next;
  portEXIT_CRITICAL(&featureMux_);
}

void AcidGlassAudio::printStatus(Print &out) const {
  out.print(F("[audio] sd="));
  out.print(sdReady_ ? F("ready") : F("off"));
  out.print(F(" engine="));
  out.print(audioReady_ ? F("ready") : F("off"));
  out.print(F(" playing="));
  out.print(playing_ ? F("yes") : F("no"));
  out.print(F(" track="));
  out.print(activeTrack_);
  out.print('/');
  out.print(trackCount_);
  out.print(F(" underruns="));
  out.print(underruns_);
  out.print(F(" status="));
  out.println(status_);
}
