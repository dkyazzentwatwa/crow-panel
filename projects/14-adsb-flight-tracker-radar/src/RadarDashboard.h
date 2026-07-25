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
// blitted here) inside an instrument bay on the left, and a distance-sorted
// aircraft list + feed health + station/clock on the right, with touch to select
// aircraft and cycle range rings.
//
// Rendering rules this class obeys, all forced by the panel having ONE directly
// scanned framebuffer and no page flip:
//   * The display is opened with manualFlush=true, so draws only touch the
//     cached framebuffer. Every painter calls markRows() for the rows it dirties
//     and tick() issues exactly one flushMarked() per frame. A painter that
//     forgets to mark leaves its region frozen on the panel forever.
//   * Anything that changes every frame lives INSIDE the scope canvas (including
//     the selected-aircraft card) so it lands in one blit and cannot tear.
//   * Everything outside the scope repaints only when its content signature
//     changes. Nothing repaints on a bare timer.
//
// Gated on USE_DISPLAY: every method no-ops with the display off so the sketch
// still runs Serial-only.
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

  // The only part of screen selection that differs with the display off, hence
  // the only one with a definition in each arm of the #if below. setScreen(),
  // nextScreen() and screenName() are defined once, outside it.
  void setScreen_(RadarScreen screen);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void handleTouch_();
  bool handleTouchAt_(int16_t tx, int16_t ty, const char *mapping);
  bool isRangeHit_(int16_t tx, int16_t ty) const;
  bool tabScreenAt_(int16_t tx, int16_t ty, RadarScreen &screen) const;
  bool changeScreenFromTouch_(RadarScreen screen, const String &where, const char *action);
  int8_t hitTestBlip_(int16_t tx, int16_t ty) const;
  int8_t hitTestRow_(int16_t tx, int16_t ty) const;
  int8_t resolveSelection_() const;
  void selectAircraft_(int8_t row);
  void clearSelection_();
  void cycleRange_();
  void drawScreenTabs_(Arduino_GFX *g);
  void drawWorldScreen_();
  void drawWorldFooter_(bool valid, unsigned long ms, const String &error);
  void drawWorldFooterForScreen_();
  void drawWeatherScreen_();
  void drawQuakeScreen_();
  void drawAuroraScreen_();
  void drawAirScreen_();
  void drawIntroStatic_();
  void drawIntroFrame_(uint32_t elapsedMs);
  void blitScope_();
  void drawHeader_();  // one painter for both the radar and world headers
  void drawBay_();
  void drawLegend_();
  void drawRuler_();
  void drawList_();
  void drawFeedStats_();
  void drawFeedSpark_();
  void drawStation_();
  void drawClock_();
  void drawFooterChrome_(bool slotDividers);
  void drawFooter_();

  // Content signatures: a region repaints when its signature changes, never on
  // a bare timer. Quantized to exactly what the printf renders, or the hash
  // churns on noise the panel cannot show and nothing ever settles.
  uint32_t headerSignature_() const;
  uint32_t listSignature_() const;
  uint32_t footerSignature_() const;
  uint32_t feedSignature_() const;
  uint32_t clockSignature_() const;

  RadarScope scope_;
  AdsbSnapshot snap_;
  int16_t blipX_[kMaxContacts];
  int16_t blipY_[kMaxContacts];
  float sweepDeg_ = 0.0f;
  uint32_t lastFrameMs_ = 0;

  // Selection is held as an ICAO address, not an index: copySnapshot() re-sorts
  // by distance every frame, so an index silently transfers the selection to a
  // different aircraft whenever two contacts cross. selectedRow_ is that ICAO
  // resolved against the current snapshot, recomputed once per frame.
  char selectedIcao_[7] = {0};
  int8_t selectedRow_ = -1;
  bool detailOpen_ = false;

  bool wasTouched_ = false;
  uint32_t lastTouchActionMs_ = 0;
  uint32_t lastScreenChangeMs_ = 0;
  bool ready_ = false;
  int16_t rangePillX_ = 0, rangePillW_ = 0;  // header range pill rect (touch)

  uint32_t introUntilMs_ = 0;  // non-zero while the boot splash owns the screen

  Throttle frameGate_{30};
  bool screenDirty_ = true;
  uint32_t headerSig_ = 0xFFFFFFFF;
  uint32_t listSig_ = 0xFFFFFFFF;
  uint32_t footerSig_ = 0xFFFFFFFF;
  uint32_t feedSig_ = 0xFFFFFFFF;
  uint32_t clockSig_ = 0xFFFFFFFF;
  Throttle clockGate_{500};
  Throttle worldRefreshGate_{30000};

  // Contacts-over-time history behind the feed-health sparkline.
  static const uint16_t kHistLen = 64;
  float contactHist_[kHistLen];
  uint16_t histTail_ = 0;   // oldest sample
  uint16_t histCount_ = 0;  // valid samples
  Throttle histGate_{5000};

  uint32_t frameAccumUs_ = 0;
  uint16_t frameCount_ = 0;
  uint32_t statWindowMs_ = 0;
  Throttle statGate_{2000};
#endif
};

#endif
