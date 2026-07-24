#ifndef PIPBOY_TERMINAL_H
#define PIPBOY_TERMINAL_H

#include "PipBoyMedia.h"
#include <Arduino.h>
#include <CrowNetworkClient.h>
#include <WorldFeed.h>
#include <WorldFeedClient.h>

enum PipBoyPage : uint8_t { kPipHome, kPipStat, kPipMap, kPipItems, kPipData, kPipRadio };

class PipBoyTerminal {
 public:
  void begin();
  void tick();
  void page(PipBoyPage page);
  void nextTrack();
  void printStatus(Print &out) const;
  void printTouch(Print &out) const;
  void commandRadio(const String &args, Print &out);
  void commandWeather(Print &out);
  void commandStorage(Print &out) const;

 private:
  PipBoyMedia media_;
  CrowNetworkClient network_;
  WorldFeedClient world_;
  WorldFeeds feeds_;
  PipBoyPage page_ = kPipHome;
  uint8_t selectedImage_ = 0;
  uint8_t selectedItem_ = 0;
  bool wasTouched_ = false;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastDynamicDrawMs_ = 0;
  uint32_t lastAnimationDrawMs_ = 0;
  uint32_t splashStartedMs_ = 0;
  uint32_t tabPulseStartedMs_ = 0;
  uint8_t splashStep_ = 0;
  int16_t lastTouchX_ = 0, lastTouchY_ = 0;
  uint32_t touchCount_ = 0;
  bool dirty_ = true;
  bool splashActive_ = false;
  void draw_();
  void drawSplash_(class Arduino_GFX *g);
  void drawHeader_(class Arduino_GFX *g);
  void drawHome_(class Arduino_GFX *g);
  void drawStat_(class Arduino_GFX *g);
  void drawMap_(class Arduino_GFX *g);
  void drawItems_(class Arduino_GFX *g);
  void drawData_(class Arduino_GFX *g);
  void drawRadio_(class Arduino_GFX *g);
  void drawStatClock_(class Arduino_GFX *g);
  void drawRadioWave_(class Arduino_GFX *g);
  void tickSplash_(class Arduino_GFX *g);
  void tickAnimation_(class Arduino_GFX *g);
  void drawActiveTabPulse_(class Arduino_GFX *g);
  void drawHomeActivity_(class Arduino_GFX *g);
  void drawStatPulse_(class Arduino_GFX *g);
  void drawMapPulse_(class Arduino_GFX *g);
  void drawItemsSweep_(class Arduino_GFX *g);
  void drawDataActivity_(class Arduino_GFX *g);
  void handleTouch_(int16_t x, int16_t y);
  void drawFallbackArt_(class Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t variant);
};

#endif
