#include "PipBoyMedia.h"

#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>

#if USE_PIPBOY_SD
#include <SD_MMC.h>
#endif
#if USE_PIPBOY_AUDIO && defined(ARDUINO_ARCH_ESP32)
#include <ESP_I2S.h>
#define PIPBOY_HAS_I2S 1
#endif
#ifndef PIPBOY_HAS_I2S
#define PIPBOY_HAS_I2S 0
#endif

namespace {
#if USE_PIPBOY_SD
File gWav;
#endif
#if PIPBOY_HAS_I2S
I2SClass gI2s;
#endif
#if USE_PIPBOY_SD
uint16_t u16(File &f) {
  uint8_t b[2] = {};
  return f.read(b, 2) == 2 ? static_cast<uint16_t>(b[0] | (b[1] << 8)) : 0;
}
uint32_t u32(File &f) {
  uint8_t b[4] = {};
  return f.read(b, 4) == 4 ? static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)) : 0;
}
#endif
bool suffix(const String &name, const char *ext) {
  String lower = name; lower.toLowerCase();
  return lower.endsWith(ext);
}
}

void PipBoyMedia::begin() {
#if USE_PIPBOY_SD
  bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  sdReady_ = alreadyMounted || SD_MMC.begin("/sdcard", PIPBOY_SDMMC_1BIT != 0);
  if (sdReady_ && SD_MMC.cardType() == CARD_NONE) sdReady_ = false;
  if (sdReady_) {
    indexDirectory_(PIPBOY_AUDIO_DIR, ".wav", tracks_, trackCount_);
    indexDirectory_(PIPBOY_IMAGE_DIR, ".bmp", images_, imageCount_);
    status_ = String("SD ready: ") + trackCount_ + " audio, " + imageCount_ + " images";
  } else {
    status_ = "SD mount failed; built-in demo only";
  }
#else
  status_ = "SD disabled; built-in demo only";
#endif
#if PIPBOY_HAS_I2S
  const AudioPins &audio = activeHardwareProfile().audio;
  pinMode(audio.control, OUTPUT);
  digitalWrite(audio.control, audio.controlActiveHigh ? HIGH : LOW);
  gI2s.setPins(audio.bclk, audio.lrclk, audio.sdata);
  audioReady_ = gI2s.begin(I2S_MODE_STD, PIPBOY_AUDIO_SAMPLE_RATE,
                           I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (!audioReady_) {
    status_ = "speaker init failed; silent";
  } else {
    status_ += "; speaker ready";
  }
#endif
}

bool PipBoyMedia::sdReady() const { return sdReady_; }
String PipBoyMedia::status() const { return status_; }
uint8_t PipBoyMedia::trackCount() const { return trackCount_; }
uint8_t PipBoyMedia::imageCount() const { return imageCount_; }
String PipBoyMedia::trackName(uint8_t index) const { return index < trackCount_ ? tracks_[index] : "NO HOLOTAPES"; }
String PipBoyMedia::imageName(uint8_t index) const { return index < imageCount_ ? images_[index] : "NO SD IMAGE"; }
bool PipBoyMedia::playing() const { return playing_; }
uint8_t PipBoyMedia::activeTrack() const { return activeTrack_; }
void PipBoyMedia::setVolume(uint8_t volume) { volume_ = min<uint8_t>(volume, 100); }
uint8_t PipBoyMedia::volume() const { return volume_; }

void PipBoyMedia::indexDirectory_(const char *directory, const char *extension, String *out, uint8_t &count) {
#if USE_PIPBOY_SD
  count = 0;
  File folder = SD_MMC.open(directory);
  if (!folder || !folder.isDirectory()) return;
  File file = folder.openNextFile();
  while (file && count < kMaxFiles) {
    String name = file.name();
    if (!file.isDirectory() && suffix(name, extension)) out[count++] = name;
    file = folder.openNextFile();
  }
#else
  (void)directory; (void)extension; (void)out; count = 0;
#endif
}

bool PipBoyMedia::openWav_(const String &path) {
#if USE_PIPBOY_SD && PIPBOY_HAS_I2S
  closeWav_();
  gWav = SD_MMC.open(path, FILE_READ);
  if (!gWav) { status_ = "holotape missing"; return false; }
  char riff[4], wave[4];
  if (gWav.readBytes(riff, 4) != 4 || u32(gWav) == 0 || gWav.readBytes(wave, 4) != 4 ||
      memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) { closeWav_(); status_ = "invalid WAV"; return false; }
  bool formatOk = false;
  while (gWav.available()) {
    char id[4]; if (gWav.readBytes(id, 4) != 4) break;
    uint32_t size = u32(gWav);
    if (!memcmp(id, "fmt ", 4)) {
      uint16_t format = u16(gWav), channels = u16(gWav); uint32_t rate = u32(gWav);
      gWav.seek(gWav.position() + 6); uint16_t bits = u16(gWav);
      if (size > 16) gWav.seek(gWav.position() + size - 16);
      formatOk = format == 1 && channels == 1 && rate == PIPBOY_AUDIO_SAMPLE_RATE && bits == 16;
    } else if (!memcmp(id, "data", 4) && formatOk) { playing_ = true; status_ = "playing holotape"; return true; }
    else gWav.seek(gWav.position() + size);
  }
  closeWav_(); status_ = "need 16-bit 16k mono WAV"; return false;
#else
  (void)path; status_ = "audio or SD disabled"; return false;
#endif
}

bool PipBoyMedia::playTrack(uint8_t index) {
  if (index >= trackCount_) return false;
  activeTrack_ = index;
  return openWav_(tracks_[index]);
}
void PipBoyMedia::closeWav_() {
#if USE_PIPBOY_SD
  if (gWav) gWav.close();
#endif
  playing_ = false;
}
void PipBoyMedia::stop() { closeWav_(); status_ = "radio stopped"; }
void PipBoyMedia::speakerTest() {
#if PIPBOY_HAS_I2S
  if (!audioReady_) { status_ = "speaker unavailable"; return; }
  toneUntilMs_ = millis() + 900;
  toneSample_ = 0;
  status_ = "speaker test";
#else
  status_ = "audio disabled";
#endif
}

void PipBoyMedia::tick() {
#if PIPBOY_HAS_I2S
  if (!audioReady_) return;
  constexpr uint16_t frames = 96;
  int16_t stereo[frames * 2] = {};
  int16_t mono[frames] = {};
#if USE_PIPBOY_SD
  if (playing_) {
    size_t got = gWav.read(reinterpret_cast<uint8_t *>(mono), sizeof(mono));
    if (got < sizeof(mono)) { closeWav_(); status_ = "holotape complete"; }
  }
#endif
  bool tone = millis() < toneUntilMs_;
  if (!playing_ && !tone) return;
  for (uint16_t i = 0; i < frames; ++i) {
    int32_t sample = static_cast<int32_t>(mono[i]) * volume_ / 100;
    if (tone) sample += ((toneSample_++ / 10) % 2 ? 1 : -1) * 5000;
    stereo[i * 2] = constrain(sample, -32768, 32767);
    stereo[i * 2 + 1] = stereo[i * 2];
  }
  gI2s.write(reinterpret_cast<uint8_t *>(stereo), sizeof(stereo));
#endif
}

bool PipBoyMedia::drawBmp_(Arduino_GFX *g, const String &path, int16_t x, int16_t y, int16_t maxW, int16_t maxH) {
#if USE_PIPBOY_SD
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) return false;
  if (u16(file) != 0x4D42) { file.close(); return false; }
  file.seek(10); uint32_t offset = u32(file); file.seek(18);
  int32_t width = static_cast<int32_t>(u32(file)), height = static_cast<int32_t>(u32(file));
  file.seek(28); uint16_t bits = u16(file);
  if (width <= 0 || height <= 0 || bits != 24 || width > maxW || height > maxH) { file.close(); return false; }
  uint32_t rowBytes = (width * 3 + 3) & ~3;
  for (int32_t row = 0; row < height; ++row) {
    file.seek(offset + (height - 1 - row) * rowBytes);
    for (int32_t col = 0; col < width; ++col) {
      uint8_t b = file.read(), green = file.read(), r = file.read();
      g->drawPixel(x + col, y + row, g->color565(r, green, b));
    }
  }
  file.close();
  return true;
#else
  (void)g; (void)path; (void)x; (void)y; (void)maxW; (void)maxH; return false;
#endif
}
bool PipBoyMedia::drawImage(Arduino_GFX *g, uint8_t index, int16_t x, int16_t y, int16_t maxW, int16_t maxH) {
  return index < imageCount_ && drawBmp_(g, images_[index], x, y, maxW, maxH);
}
bool PipBoyMedia::drawMap(Arduino_GFX *g, int16_t x, int16_t y, int16_t maxW, int16_t maxH) {
  return drawBmp_(g, PIPBOY_MAP_PATH, x, y, maxW, maxH);
}
