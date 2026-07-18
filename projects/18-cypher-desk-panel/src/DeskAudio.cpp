#include "DeskAudio.h"

#include <CrowPanelShared.h>

#if USE_CYPHER_DESK_AUDIO && defined(ARDUINO_ARCH_ESP32)
#define CYPHER_DESK_HAS_I2S 1
#include <ESP_I2S.h>
#endif

#ifndef CYPHER_DESK_HAS_I2S
#define CYPHER_DESK_HAS_I2S 0
#endif

#if USE_CYPHER_DESK_SD
#include <SD_MMC.h>
#endif

namespace {

#if CYPHER_DESK_HAS_I2S
I2SClass gDeskI2s;
#endif
#if USE_CYPHER_DESK_SD
File gDeskWav;
#endif

const char *kAmbienceNames[] = {"Off", "Rainy Cafe", "Vinyl Room", "Fireplace", "Brown Noise"};
#if CYPHER_DESK_HAS_I2S && USE_CYPHER_DESK_SD
const char *kAmbienceFiles[] = {"", "rainy-cafe.wav", "vinyl-room.wav", "fireplace.wav", "brown-noise.wav"};
#endif
const char *kKeyNames[] = {"Off", "Pencil", "Typewriter", "Mechanical"};

#if CYPHER_DESK_HAS_I2S && USE_CYPHER_DESK_SD
uint16_t readU16(File &file) {
  uint8_t b[2] = {};
  if (file.read(b, 2) != 2) return 0;
  return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

uint32_t readU32(File &file) {
  uint8_t b[4] = {};
  if (file.read(b, 4) != 4) return 0;
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
         (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}
#endif

}  // namespace

bool DeskAudio::begin() {
#if CYPHER_DESK_HAS_I2S
  const HardwareProfile &profile = activeHardwareProfile();
  pinMode(profile.audio.control, OUTPUT);
  digitalWrite(profile.audio.control, profile.audio.controlActiveHigh ? HIGH : LOW);
  gDeskI2s.setPins(profile.audio.bclk, profile.audio.lrclk, profile.audio.sdata);
  ready_ = gDeskI2s.begin(I2S_MODE_STD, CYPHER_DESK_AUDIO_SAMPLE_RATE,
                          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  status_ = ready_ ? "audio initialized" : "audio init failed; silent";
#else
  status_ = USE_CYPHER_DESK_AUDIO ? "audio driver unavailable; silent" : "audio disabled";
#endif
  return ready_;
}

void DeskAudio::setAmbience(uint8_t ambience) {
  closeAmbience();
  ambience_ = ambience % 5;
  if (ambience_ && ready_) openAmbience();
}
void DeskAudio::setKeySound(uint8_t sound) { keySound_ = sound % 4; }
void DeskAudio::setVolume(uint8_t volume) { volume_ = volume > 100 ? 100 : volume; }
void DeskAudio::triggerKey() {
  if (ready_ && keySound_) keyFrame_ = 1;
}
uint8_t DeskAudio::ambience() const { return ambience_; }
uint8_t DeskAudio::keySound() const { return keySound_; }
bool DeskAudio::ready() const { return ready_; }
String DeskAudio::status() const { return status_; }
const char *DeskAudio::ambienceName() const { return kAmbienceNames[ambience_]; }
const char *DeskAudio::keySoundName() const { return kKeyNames[keySound_]; }

bool DeskAudio::openAmbience() {
#if CYPHER_DESK_HAS_I2S && USE_CYPHER_DESK_SD
  String path = String(CYPHER_DESK_AUDIO_DIR) + "/" + kAmbienceFiles[ambience_];
  gDeskWav = SD_MMC.open(path, FILE_READ);
  if (!gDeskWav) {
    status_ = "ambience missing; silent";
    ambience_ = 0;
    return false;
  }
  char riff[4], wave[4];
  if (gDeskWav.readBytes(riff, 4) != 4 || readU32(gDeskWav) == 0 ||
      gDeskWav.readBytes(wave, 4) != 4 || memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) {
    closeAmbience();
    status_ = "invalid WAV; silent";
    ambience_ = 0;
    return false;
  }
  bool formatOk = false;
  while (gDeskWav.available()) {
    char id[4];
    if (gDeskWav.readBytes(id, 4) != 4) break;
    uint32_t size = readU32(gDeskWav);
    if (!memcmp(id, "fmt ", 4)) {
      uint16_t format = readU16(gDeskWav);
      uint16_t channels = readU16(gDeskWav);
      uint32_t rate = readU32(gDeskWav);
      gDeskWav.seek(gDeskWav.position() + 6);
      uint16_t bits = readU16(gDeskWav);
      if (size > 16) gDeskWav.seek(gDeskWav.position() + size - 16);
      formatOk = format == 1 && channels == 1 && rate == CYPHER_DESK_AUDIO_SAMPLE_RATE && bits == 16;
    } else if (!memcmp(id, "data", 4)) {
      if (!formatOk) break;
      dataStart_ = gDeskWav.position();
      dataRemaining_ = size;
      ambienceOpen_ = true;
      status_ = String(kAmbienceNames[ambience_]) + " playing";
      return true;
    } else {
      gDeskWav.seek(gDeskWav.position() + size);
    }
  }
  closeAmbience();
  status_ = "unsupported WAV; silent";
  ambience_ = 0;
#endif
  return false;
}

void DeskAudio::closeAmbience() {
#if USE_CYPHER_DESK_SD
  if (gDeskWav) gDeskWav.close();
#endif
  ambienceOpen_ = false;
  dataRemaining_ = 0;
}

void DeskAudio::tick() {
#if CYPHER_DESK_HAS_I2S
  if (!ready_ || (!ambienceOpen_ && keyFrame_ == 0)) return;
  constexpr uint16_t kFrames = 96;
  int16_t mono[kFrames] = {};
#if USE_CYPHER_DESK_SD
  if (ambienceOpen_) {
    size_t wanted = dataRemaining_ < sizeof(mono) ? dataRemaining_ : sizeof(mono);
    size_t got = gDeskWav.read(reinterpret_cast<uint8_t *>(mono), wanted);
    dataRemaining_ -= got;
    if (got < sizeof(mono)) memset(reinterpret_cast<uint8_t *>(mono) + got, 0, sizeof(mono) - got);
    if (dataRemaining_ == 0) {
      gDeskWav.seek(dataStart_);
      dataRemaining_ = gDeskWav.size() - dataStart_;
    }
  }
#endif
  int16_t stereo[kFrames * 2];
  for (uint16_t i = 0; i < kFrames; ++i) {
    int32_t sample = static_cast<int32_t>(mono[i]) * volume_ / 100;
    if (keyFrame_) {
      uint16_t frame = keyFrame_ + i;
      if (frame < 180) {
        int32_t envelope = (180 - frame) * volume_ * 2;
        int32_t generated = 0;
        if (keySound_ == 1) generated = ((frame * 37) % 23 - 11) * envelope / 30;
        if (keySound_ == 2) generated = ((frame / 4) % 2 ? -1 : 1) * envelope;
        if (keySound_ == 3) generated = ((frame / 2) % 2 ? -1 : 1) * envelope / 2;
        sample += generated;
      }
    }
    sample = constrain(sample, -32768, 32767);
    stereo[i * 2] = sample;
    stereo[i * 2 + 1] = sample;
  }
  if (keyFrame_) {
    keyFrame_ += kFrames;
    if (keyFrame_ >= 180) keyFrame_ = 0;
  }
  gDeskI2s.write(reinterpret_cast<const uint8_t *>(stereo), sizeof(stereo));
#endif
}
