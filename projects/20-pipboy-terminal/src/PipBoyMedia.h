#ifndef PIPBOY_TERMINAL_MEDIA_H
#define PIPBOY_TERMINAL_MEDIA_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class Arduino_GFX;

class PipBoyMedia {
 public:
  void begin();
  void tick();
  bool sdReady() const;
  String status() const;
  uint8_t trackCount() const;
  uint8_t imageCount() const;
  String trackName(uint8_t index) const;
  String imageName(uint8_t index) const;
  bool playTrack(uint8_t index);
  void stop();
  bool playing() const;
  uint8_t activeTrack() const;
  void setVolume(uint8_t volume);
  uint8_t volume() const;
  bool drawImage(Arduino_GFX *g, uint8_t index, int16_t x, int16_t y, int16_t maxW, int16_t maxH);
  bool drawMap(Arduino_GFX *g, int16_t x, int16_t y, int16_t maxW, int16_t maxH);
  void speakerTest();

 private:
  static constexpr uint8_t kMaxFiles = 16;
  String tracks_[kMaxFiles];
  String images_[kMaxFiles];
  uint8_t trackCount_ = 0;
  uint8_t imageCount_ = 0;
  bool sdReady_ = false;
  bool audioReady_ = false;
  bool playing_ = false;
  bool autoplay_ = false;
  void *txChan_ = nullptr;
  uint8_t activeTrack_ = 0;
  uint8_t volume_ = 70;
  uint32_t toneUntilMs_ = 0;
  uint32_t toneSample_ = 0;
  String status_ = "media disabled";
  bool openWav_(const String &path);
  void closeWav_();
  void advance_();
  bool drawBmp_(Arduino_GFX *g, const String &path, int16_t x, int16_t y, int16_t maxW, int16_t maxH);
  void indexDirectory_(const char *directory, const char *extension, String *out, uint8_t &count);
};

#endif
