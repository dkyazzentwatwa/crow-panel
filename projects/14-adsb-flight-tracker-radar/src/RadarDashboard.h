#ifndef ADSB_RADAR_DASHBOARD_H
#define ADSB_RADAR_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <WorldFeed.h>
#include "AdsbTypes.h"
#include "AircraftStore.h"

enum RadarScreen : uint8_t {
  kRadarScreen = 0,
  kWeatherScreen,
  kQuakeScreen,
  kAuroraScreen,
  kAirScreen,
  kRadarScreenCount
};

// Full-screen radar dashboard: the animated scope (composited by RadarScope and
// blitted here) on the left, and a distance-sorted aircraft list + location +
// NTP clock on the right, with touch to select aircraft and cycle range rings.
// Gated on USE_DISPLAY: every method no-ops with the display off so the sketch
// still runs Serial-only. Mirrors ControlHubDashboard's begin/tick/repaint +
// dirty-flag + touch-edge structure.
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#include "RadarScope.h"
#endif

class RadarDashboard {
 public:
  void begin(uint16_t initialRangeKm);
  void tick(AircraftStore &store);
  void setRangeKm(uint16_t km);  // serial `range` command
  uint16_t rangeKm() const { return rangeRingKm_; }
  void setWorldFeeds(const WorldFeeds &feeds);
  void nextScreen();
  bool setScreen(const String &name);
  const char *screenName() const;

 private:
  uint16_t rangeRingKm_ = 100;  // always present so `range`/`status` work headless
  RadarScreen screen_ = kRadarScreen;
  WorldFeeds world_;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void handleTouch_();
  bool handleTouchAt_(int16_t tx, int16_t ty, const char *mapping);
  bool isNextHit_(int16_t tx, int16_t ty) const;
  bool isRangeHit_(int16_t tx, int16_t ty) const;
  bool tabScreenAt_(int16_t tx, int16_t ty, RadarScreen &screen) const;
  bool changeScreenFromTouch_(RadarScreen screen, const String &where, const char *action);
  int8_t hitTestBlip_(int16_t tx, int16_t ty) const;
  int8_t hitTestRow_(int16_t tx, int16_t ty) const;
  void cycleRange_();
  void setScreen_(RadarScreen screen);
  void drawScreenTabs_(Arduino_GFX *g);
  void drawWorldScreen_();
  void drawWorldHeader_(const char *title, const char *subtitle);
  void drawWorldFooter_(bool valid, unsigned long ms, const String &error);
  void drawWeatherScreen_();
  void drawQuakeScreen_();
  void drawAuroraScreen_();
  void drawAirScreen_();
  void drawIntroSplash_();
  void drawIntroStatic_();
  void drawIntroFrame_(uint32_t elapsedMs);
  void blitScope_();
  void drawHeader_();
  void drawList_();
  void drawLocation_();
  void drawClock_();
  void drawFooter_();
  void drawDetail_();

  RadarScope scope_;
  AdsbSnapshot snap_;
  int16_t blipX_[kMaxContacts];
  int16_t blipY_[kMaxContacts];
  float sweepDeg_ = 0.0f;
  uint32_t lastFrameMs_ = 0;
  int8_t selectedIdx_ = -1;
  bool detailOpen_ = false;
  bool wasTouched_ = false;
  uint32_t lastTouchActionMs_ = 0;
  uint32_t lastScreenChangeMs_ = 0;
  bool ready_ = false;
  int16_t rangePillX_ = 0, rangePillW_ = 0;  // header range pill rect (touch)
  int16_t nextPillX_ = 0, nextPillW_ = 0;     // header NEXT pill rect (touch)
  int16_t detailX_ = 0, detailY_ = 0, detailW_ = 0, detailH_ = 0;
  Throttle frameGate_{30};
  uint32_t chromeSig_ = 0xFFFFFFFF;
  bool screenDirty_ = true;
  Throttle listRefreshGate_{2000};
  Throttle clockGate_{1000};
  Throttle worldRefreshGate_{30000};
  uint32_t frameAccumUs_ = 0;
  uint16_t frameCount_ = 0;
  Throttle statGate_{2000};
#endif
};

#endif
