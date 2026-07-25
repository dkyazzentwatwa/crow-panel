#include "RadarDashboard.h"

// ---------------------------------------------------------------------------
// Screen selection. Identical with and without the display: the only piece that
// touches the panel is setScreen_, so that is the only one defined twice (once
// in each arm of the #if below) and these three are defined once, here.
// ---------------------------------------------------------------------------

namespace {
const char *screenNameFor(RadarScreen screen) {
  switch (screen) {
    case kRadarScreen: return "radar";
    case kWeatherScreen: return "weather";
    case kQuakeScreen: return "quake";
    case kAuroraScreen: return "aurora";
    case kAirScreen: return "air";
    default: return "unknown";
  }
}
}  // namespace

const char *RadarDashboard::screenName() const { return screenNameFor(screen_); }

void RadarDashboard::nextScreen() {
  RadarScreen next = (RadarScreen)(((uint8_t)screen_ + 1) % (uint8_t)kRadarScreenCount);
  setScreen_(next);
}

bool RadarDashboard::setScreen(const String &name) {
  String n = name;
  n.trim();
  n.toLowerCase();
  if (n == "next" || n.length() == 0) {
    nextScreen();
    return true;
  }
  if (n == "radar" || n == "adsb" || n == "planes") {
    setScreen_(kRadarScreen);
    return true;
  }
  if (n == "weather" || n == "wx") {
    setScreen_(kWeatherScreen);
    return true;
  }
  if (n == "quake" || n == "quakes" || n == "earthquake" || n == "earthquakes") {
    setScreen_(kQuakeScreen);
    return true;
  }
  if (n == "aurora" || n == "kp" || n == "space") {
    setScreen_(kAuroraScreen);
    return true;
  }
  if (n == "air" || n == "aqi" || n == "airquality") {
    setScreen_(kAirScreen);
    return true;
  }
  return false;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "AdsbFormat.h"

using namespace Widgets;

namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 56;

// --- Left: the instrument bay. The disc used to float in a 1024x600 field with
// 76 px of nothing to its left; wrapping it in a bordered card that spans the
// full left half turns background into bezel, and the slack each side reads as
// deliberate padding. The bay also reaches down into what was dead space below
// the disc, which is where the legend and the range ruler live.
constexpr int16_t kBayX = 16;
constexpr int16_t kBayY = 66;
constexpr int16_t kBayW = 488;
constexpr int16_t kBayH = 472;  // 66 .. 538
constexpr int16_t kBayInX = kBayX + 16;
constexpr int16_t kBayInW = kBayW - 32;  // 456
constexpr int16_t kScopeW = 360;         // small enough to fit fast internal SRAM
constexpr int16_t kScopeH = 360;
constexpr int16_t kScopeX = kBayX + (kBayW - kScopeW) / 2;  // 80
constexpr int16_t kScopeY = kBayY + 34;                     // 100 .. 460
constexpr int16_t kLegendY = 470;
constexpr int16_t kLegendH = 26;
constexpr int16_t kRulerY = 504;
constexpr int16_t kRulerH = 32;

// --- Right: the data column.
constexpr int16_t kPanelX = 520;
constexpr int16_t kPanelW = kScreenW - kPanelX - 16;  // 488
constexpr int16_t kListY = 66;
constexpr int16_t kListHeadH = 30;
constexpr int16_t kListTop = kListY + 36;  // 102
constexpr int16_t kRowH = 40;
constexpr uint8_t kMaxRows = 6;
constexpr int16_t kListH = kListTop + kMaxRows * kRowH - kListY;  // 66 .. 342
constexpr int16_t kFeedY = 362;
constexpr int16_t kFeedH = 86;  // 362 .. 448
constexpr int16_t kFeedStatsX = kPanelX + 248;
constexpr int16_t kFeedStatsY = kFeedY + 18;
constexpr int16_t kFeedStatsH = 62;
constexpr int16_t kStationY = 458;
constexpr int16_t kStationH = 80;  // 458 .. 538
// Inset from the station card's edges so clearing the digits never squares off
// its rounded corners.
constexpr int16_t kClockX = kPanelX + 248;
constexpr int16_t kClockY = kStationY + 26;
constexpr int16_t kClockW = kPanelW - 248 - 14;  // 226
constexpr int16_t kClockH = 46;

constexpr int16_t kFooterY = 548;
constexpr int16_t kFooterH = 52;
constexpr int16_t kFooterSlotY = kFooterY + 8;
constexpr int16_t kFooterSlotH = 36;
constexpr int16_t kFooterSlots = 4;

constexpr int16_t kTabsX = 380;
constexpr int16_t kTabsY = 14;
constexpr int16_t kTabH = 28;
constexpr int16_t kTabGap = 8;
constexpr int16_t kTabWellX = kTabsX - 6;
constexpr int16_t kTabWellY = kTabsY - 4;
constexpr int16_t kTabWellH = kTabH + 8;

constexpr uint16_t kTouchDebounceMs = 120;
constexpr uint16_t kScreenChangeCooldownMs = 1100;
constexpr int16_t kBlipHitRadius = 36;  // px; squared before comparing
constexpr int16_t kRowHitPadY = 8;
constexpr int16_t kRangePillPadX = 22;

constexpr int16_t kWorldTop = 76;
constexpr int16_t kWorldLeft = 48;
constexpr int16_t kWorldRight = kScreenW - 48;
constexpr int16_t kWorldW = kWorldRight - kWorldLeft;
constexpr uint32_t kIntroSplashMs = 8000;
constexpr uint32_t kWorldStaleMs = 45UL * 60UL * 1000UL;
constexpr uint16_t kSweepMsPerRev = 4500;
constexpr float kDegPerMs = 360.0f / (float)kSweepMsPerRev;

constexpr uint16_t kRangeSteps[] = {20, 40, 60, 80, 100};
constexpr uint8_t kRangeStepCount = sizeof(kRangeSteps) / sizeof(kRangeSteps[0]);

// --- Dirty-row accumulator -------------------------------------------------
// The panel is opened with manualFlush=true, so nothing reaches the glass until
// esp_cache_msync runs. CrowDisplay::flush(x,y,w,h) IGNORES x and w and syncs
// whole rows (the framebuffer is row-contiguous), so the cheapest correct thing
// is to accumulate one row RANGE per frame and issue a single flush - the
// regions here overlap heavily in Y, so per-band flushing would sync the same
// rows twice and hand the scanout a second chance to catch a half-written area.
int16_t gFlushTop = 0x7FFF;
int16_t gFlushBottom = -1;

void markRows(int16_t y, int16_t h) {
  if (y < gFlushTop) gFlushTop = y;
  if (y + h > gFlushBottom) gFlushBottom = y + h;
}

void flushMarked() {
  if (gFlushBottom > gFlushTop) {
    CrowDisplay::flush(0, gFlushTop, kScreenW, gFlushBottom - gFlushTop);
  }
  gFlushTop = 0x7FFF;
  gFlushBottom = -1;
}

// FNV-1a over already-quantized values. Used for the per-region content
// signatures so a region repaints when what it DISPLAYS changes, not when the
// underlying float wobbles below the printed precision.
inline uint32_t hashBytes(uint32_t h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  while (n--) {
    h ^= *b++;
    h *= 16777619u;
  }
  return h;
}
constexpr uint32_t kHashSeed = 2166136261u;

String fit(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  return RadarScope::fit(g, s, font, maxW);
}

bool numberReady(float value) { return !isnan(value); }

String fmtFloat(float value, uint8_t decimals, const char *suffix = "") {
  if (!numberReady(value)) return "--";
  return String(value, (unsigned int)decimals) + suffix;
}

String feedAge(unsigned long ms) {
  if (ms == 0) return "never updated";
  unsigned long age = (millis() - ms) / 1000UL;
  if (age < 90) return String(age) + "s ago";
  if (age < 7200) return String(age / 60UL) + "m ago";
  return String(age / 3600UL) + "h ago";
}

void metricCard(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h,
                const char *label, const String &big, const String &sub,
                uint16_t accent) {
  panel(g, x, y, w, h, 8, kSurface);
  text(g, x + 16, y + 12, label, fontS(), accent, kLeft);
  text(g, x + 16, y + 38, fit(g, big, fontXL(), w - 32).c_str(), fontXL(), kTextHi, kLeft);
  if (sub.length() > 0) {
    text(g, x + 16, y + h - 32, fit(g, sub, fontS(), w - 32).c_str(), fontS(), kTextMut, kLeft);
  }
}

uint16_t quakeColor(float mag) {
  if (!numberReady(mag)) return kLine;
  if (mag >= 6.0f) return kRed;
  if (mag >= 5.0f) return kAmber;
  return kAccent;
}

uint16_t aqiColor(float aqi) {
  if (!numberReady(aqi)) return kLine;
  if (aqi <= 50.0f) return kGreen;
  if (aqi <= 100.0f) return kAmber;
  return kRed;
}

const char *screenTitleFor(RadarScreen screen) {
  switch (screen) {
    case kRadarScreen: return "ADS-B RADAR";
    case kWeatherScreen: return "WEATHER";
    case kQuakeScreen: return "EARTHQUAKES";
    case kAuroraScreen: return "AURORA WATCH";
    case kAirScreen: return "AIR QUALITY";
    default: return "DASHBOARD";
  }
}

const char *screenTabLabelFor(RadarScreen screen) {
  switch (screen) {
    case kRadarScreen: return "RADAR";
    case kWeatherScreen: return "WX";
    case kQuakeScreen: return "QUAKE";
    case kAuroraScreen: return "AURORA";
    case kAirScreen: return "AIR";
    default: return "?";
  }
}

int16_t screenTabWidthFor(RadarScreen screen) {
  switch (screen) {
    case kRadarScreen: return 66;
    case kWeatherScreen: return 46;
    case kQuakeScreen: return 72;
    case kAuroraScreen: return 86;
    case kAirScreen: return 48;
    default: return 48;
  }
}

const char *screenSubtitleFor(RadarScreen screen) {
  switch (screen) {
    case kRadarScreen: return "LIVE AIRCRAFT TRACKER";
    case kWeatherScreen: return "OPEN-METEO FORECAST";
    case kQuakeScreen: return "USGS M4.5+ PAST DAY";
    case kAuroraScreen: return "NOAA PLANETARY K-INDEX";
    case kAirScreen: return "OPEN-METEO AIR QUALITY";
    default: return "PUBLIC WORLD FEED";
  }
}
}  // namespace

void RadarDashboard::begin(uint16_t initialRangeKm) {
  rangeRingKm_ = initialRangeKm;
  // manualFlush=true: draws land in the cached framebuffer and nothing reaches
  // the panel until flushMarked(). That turns Arduino_GFX's per-primitive
  // esp_cache_msync (two bytes per pixel through writePixelPreclipped, which is
  // what every glyph goes through) into one sync per frame.
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "ADS-B Flight Radar",
                              /*manualFlush=*/true) &&
           (CrowDisplay::canvas() != nullptr);
  if (!ready_) return;

  if (!scope_.begin(kScopeW, kScopeH)) {
    Logger::error("radar", "scope buffer alloc failed; running chrome-only");
  }
  Logger::info("radar", String("scope ") + kScopeW + "x" + kScopeH + " in " +
                            (scope_.bufferInternal() ? "internal-SRAM" : "PSRAM") + "; free int " +
                            String(ESP.getFreeHeap() / 1024) + "KB / psram " +
                            String(ESP.getFreePsram() / 1024) + "KB");
  configTzTime(ADSB_TZ, "pool.ntp.org", "time.nist.gov");

  memset(blipX_, 0xFF, sizeof(blipX_));  // -1
  memset(blipY_, 0xFF, sizeof(blipY_));
  memset(&snap_, 0, sizeof(snap_));

  // The splash is a tick() state, not a blocking loop: it used to hold setup()
  // for eight seconds, which delayed Wi-Fi bring-up and meant the serial router
  // was not even registered yet.
  introUntilMs_ = millis() + kIntroSplashMs;
  drawIntroStatic_();
  flushMarked();

  lastFrameMs_ = millis();
  statWindowMs_ = lastFrameMs_;
  screenDirty_ = true;  // first radar tick paints the whole chrome, in one place
}

void RadarDashboard::setRangeKm(uint16_t km) { rangeRingKm_ = km; }

void RadarDashboard::setWorldFeeds(const WorldFeeds &feeds) {
  world_ = feeds;
  if (screen_ != kRadarScreen) screenDirty_ = true;
}

void RadarDashboard::setScreen_(RadarScreen screen) {
  if (screen >= kRadarScreenCount) screen = kRadarScreen;
  if (screen_ == screen && !screenDirty_) return;
  screen_ = screen;
  clearSelection_();
  headerSig_ = listSig_ = footerSig_ = feedSig_ = clockSig_ = 0xFFFFFFFF;
  screenDirty_ = true;
  Logger::info("screen", String("show ") + screenNameFor(screen_));
}

void RadarDashboard::tick(AircraftStore &store) {
  if (!ready_) return;

  // Boot splash owns the screen until it expires. Serial and Wi-Fi run
  // normally underneath because this is a state, not a blocking loop.
  if (introUntilMs_ != 0) {
    uint32_t nowMs = millis();
    if ((int32_t)(nowMs - introUntilMs_) < 0) {
      if (frameGate_.ready()) {
        drawIntroFrame_(kIntroSplashMs - (introUntilMs_ - nowMs));
        flushMarked();
      }
      return;
    }
    introUntilMs_ = 0;
    screenDirty_ = true;
    lastFrameMs_ = nowMs;
  }

  handleTouch_();

  if (screen_ != kRadarScreen) {
    if (screenDirty_) {
      drawWorldScreen_();
      screenDirty_ = false;
    } else if (worldRefreshGate_.ready()) {
      // Only the "updated Ns ago" string ages. Repainting the whole screen for
      // it meant a full-screen black flash every 30 seconds.
      drawWorldFooterForScreen_();
    }
    flushMarked();
    return;
  }

  if (!frameGate_.ready()) return;

  uint32_t now = millis();
  float dt = (float)(now - lastFrameMs_);
  lastFrameMs_ = now;
  sweepDeg_ += dt * kDegPerMs;
  while (sweepDeg_ >= 360.0f) sweepDeg_ -= 360.0f;

  store.copySnapshot(snap_);
  snap_.rangeRingKm = rangeRingKm_;
  selectedRow_ = resolveSelection_();
  if (selectedRow_ < 0) clearSelection_();

  bool sparkDirty = false;
  if (histGate_.ready()) {
    contactHist_[(histTail_ + histCount_) % kHistLen] = (float)snap_.count;
    if (histCount_ < kHistLen) {
      histCount_++;
    } else {
      histTail_ = (histTail_ + 1) % kHistLen;
    }
    sparkDirty = true;
  }

  uint32_t t0 = micros();

  // One dispatch point for every non-scope region. Each is painted either
  // because the whole screen is dirty or because its own content signature
  // moved - never twice in the same frame, and never on a bare timer.
  uint32_t hSig = headerSignature_();
  uint32_t lSig = listSignature_();
  uint32_t fSig = footerSignature_();
  uint32_t dSig = feedSignature_();

  if (screenDirty_) {
    CrowDisplay::canvas()->fillScreen(kBg);
    markRows(0, kScreenH);
    drawHeader_();
    drawBay_();
    drawLegend_();
    drawRuler_();
    drawList_();
    drawFeedSpark_();
    drawFeedStats_();
    drawStation_();
    drawClock_();
    drawFooterChrome_(/*slotDividers=*/true);
    drawFooter_();
    headerSig_ = hSig;
    listSig_ = lSig;
    footerSig_ = fSig;
    feedSig_ = dSig;
    clockSig_ = clockSignature_();
    screenDirty_ = false;
  } else {
    if (hSig != headerSig_) {
      headerSig_ = hSig;
      drawHeader_();
      drawRuler_();  // the ruler is scaled to the range ring
    }
    if (lSig != listSig_) {
      listSig_ = lSig;
      drawList_();
    }
    if (fSig != footerSig_) {
      footerSig_ = fSig;
      drawFooter_();
    }
    // The sparkline only gains a sample every histGate_ tick; the stat rows age
    // once a second. Repainting the whole card for the age string would redraw
    // the sparkline 5x more often than it can possibly change.
    if (sparkDirty) {
      drawFeedSpark_();   // repaints the whole card fill...
      drawFeedStats_();   // ...so the stat rows have to follow it unconditionally
      feedSig_ = dSig;
    } else if (dSig != feedSig_) {
      feedSig_ = dSig;
      drawFeedStats_();
    }
    if (clockGate_.ready()) {
      uint32_t cSig = clockSignature_();
      if (cSig != clockSig_) {
        clockSig_ = cSig;
        drawClock_();
      }
    }
  }

  // The scope and the detail card are composited into the same offscreen buffer
  // and reach the panel in ONE blit. Drawing the card separately is what made it
  // strobe: it sits inside the scope rect, so every blit erased it and every
  // repaint drew it back, 30 times a second.
  bool cardOpen = detailOpen_ && selectedRow_ >= 0;
  scope_.render(snap_, sweepDeg_, selectedRow_, blipX_, blipY_, cardOpen);
  if (cardOpen) scope_.renderDetail(snap_.ac[selectedRow_]);
  blitScope_();

  flushMarked();

  // Accounted AFTER the flush: post-migration the cache sync is the dominant
  // per-frame cost, so stopping the clock at the blit would flatter the number.
  frameAccumUs_ += (micros() - t0);
  frameCount_++;
  if (statGate_.ready() && frameCount_ > 0) {
    uint32_t win = now - statWindowMs_;
    Logger::info("radar", "render " + String(frameAccumUs_ / frameCount_ / 1000.0f, 1) +
                              " ms/frame (" +
                              String(win ? frameCount_ * 1000.0f / (float)win : 0.0f, 1) +
                              " fps), contacts " + String(snap_.count));
    frameAccumUs_ = 0;
    frameCount_ = 0;
    statWindowMs_ = now;
  }
}

void RadarDashboard::blitScope_() {
  uint16_t *fb = scope_.framebuffer();
  if (!fb) return;
  CrowDisplay::canvas()->draw16bitRGBBitmap(kScopeX, kScopeY, fb, scope_.width(), scope_.height());
  markRows(kScopeY, scope_.height());
}

// The selection is an ICAO address; resolve it against the current (re-sorted)
// snapshot. Returns -1 when the aircraft has aged out of the store.
int8_t RadarDashboard::resolveSelection_() const {
  if (selectedIcao_[0] == '\0') return -1;
  for (uint8_t i = 0; i < snap_.count; i++) {
    if (strncmp(snap_.ac[i].icao, selectedIcao_, sizeof(selectedIcao_)) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

void RadarDashboard::selectAircraft_(int8_t row) {
  if (row < 0 || row >= (int8_t)snap_.count) return;
  strncpy(selectedIcao_, snap_.ac[row].icao, sizeof(selectedIcao_) - 1);
  selectedIcao_[sizeof(selectedIcao_) - 1] = '\0';
  selectedRow_ = row;
  detailOpen_ = true;
}

void RadarDashboard::clearSelection_() {
  selectedIcao_[0] = '\0';
  selectedRow_ = -1;
  detailOpen_ = false;
}

void RadarDashboard::handleTouch_() {
  int16_t rx, ry;
  bool touched = CrowDisplay::touchPoint(rx, ry);
  uint32_t now = millis();
  bool shouldProcess = touched && !wasTouched_ && (now - lastTouchActionMs_) > kTouchDebounceMs;
  if (shouldProcess) {
#if ADSB_TOUCH_AUTOPROBE
    // Bring-up escape hatch only: try every orientation and log which one lands
    // on a control. Never ship with this on - see ProjectConfig.h.
    struct TouchCandidate {
      int16_t x;
      int16_t y;
      const char *name;
    };
    const TouchCandidate candidates[] = {
        {rx, ry, "raw"},
        {ry, rx, "swap"},
        {(int16_t)(kScreenW - 1 - rx), ry, "flipX"},
        {rx, (int16_t)(kScreenH - 1 - ry), "flipY"},
        {(int16_t)(kScreenW - 1 - rx), (int16_t)(kScreenH - 1 - ry), "flipXY"},
        {(int16_t)(kScreenW - 1 - ry), rx, "swapFlipX"},
        {ry, (int16_t)(kScreenH - 1 - rx), "swapFlipY"},
        {(int16_t)(kScreenW - 1 - ry), (int16_t)(kScreenH - 1 - rx), "swapFlipXY"},
    };
    bool handled = false;
    for (uint8_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      int16_t tx = candidates[i].x;
      int16_t ty = candidates[i].y;
      if (tx < 0 || tx >= kScreenW || ty < 0 || ty >= kScreenH) continue;
      bool duplicate = false;
      for (uint8_t j = 0; j < i; j++) {
        if (candidates[j].x == tx && candidates[j].y == ty) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      if (handleTouchAt_(tx, ty, candidates[i].name)) {
        handled = true;
        break;
      }
    }
#else
    // One calibrated mapping: swap, then invert. See ProjectConfig.h.
#if ADSB_TOUCH_SWAP_XY
    int16_t tx = ry, ty = rx;
#else
    int16_t tx = rx, ty = ry;
#endif
#if ADSB_TOUCH_INVERT_X
    tx = (int16_t)(kScreenW - 1 - tx);
#endif
#if ADSB_TOUCH_INVERT_Y
    ty = (int16_t)(kScreenH - 1 - ty);
#endif
    bool handled = (tx >= 0 && tx < kScreenW && ty >= 0 && ty < kScreenH) &&
                   handleTouchAt_(tx, ty, "cal");
#endif

    if (!handled) {
      Logger::info("touch", "ignored raw=" + String(rx) + "," + String(ry));
    }
    lastTouchActionMs_ = now;
  }
  wasTouched_ = touched;
}

bool RadarDashboard::isRangeHit_(int16_t tx, int16_t ty) const {
  if (tx < 0 || tx >= kScreenW || ty < 0 || ty >= kScreenH) return false;
  return rangePillX_ > 0 &&
         tx >= rangePillX_ - kRangePillPadX && tx <= rangePillX_ + rangePillW_ + kRangePillPadX &&
         ty >= 8 && ty <= kHeaderH - 4;
}

bool RadarDashboard::tabScreenAt_(int16_t tx, int16_t ty, RadarScreen &screen) const {
  if (tx < 0 || tx >= kScreenW || ty < kTabsY - 4 || ty > kTabsY + kTabH + 4) return false;

  int16_t x = kTabsX;
  for (uint8_t i = 0; i < (uint8_t)kRadarScreenCount; i++) {
    RadarScreen candidate = (RadarScreen)i;
    int16_t w = screenTabWidthFor(candidate);
    if (tx >= x - 2 && tx <= x + w + 2) {
      screen = candidate;
      return true;
    }
    x += w + kTabGap;
  }
  return false;
}

bool RadarDashboard::changeScreenFromTouch_(RadarScreen screen, const String &where,
                                            const char *action) {
  if (screen >= kRadarScreenCount) screen = kRadarScreen;
  if (screen == screen_) {
    Logger::info("touch", where + " action=" + action + "-current");
    return true;
  }

  uint32_t now = millis();
  if ((now - lastScreenChangeMs_) < kScreenChangeCooldownMs) {
    Logger::info("touch", where + " action=" + action + "-cooldown");
    return true;
  }

  setScreen_(screen);
  lastScreenChangeMs_ = now;
  Logger::info("touch", where + " action=" + action + " screen=" + screenNameFor(screen));
  return true;
}

bool RadarDashboard::handleTouchAt_(int16_t tx, int16_t ty, const char *mapping) {
  String where = String("map=") + mapping + " x=" + tx + " y=" + ty;
  RadarScreen tabScreen = kRadarScreen;

  if (tabScreenAt_(tx, ty, tabScreen)) {
    return changeScreenFromTouch_(tabScreen, where, "tab");
  }

  if (screen_ == kRadarScreen && isRangeHit_(tx, ty)) {
    cycleRange_();
    // No screenDirty_: the header signature covers the pill and the ruler, and
    // the scope re-renders its rings every frame anyway. A full-screen
    // fillScreen here was a whole-panel black flash for a two-word change.
    Logger::info("touch", where + " action=range");
    return true;
  }

  if (screen_ != kRadarScreen) return false;

  if (detailOpen_) {
    clearSelection_();
    Logger::info("touch", where + " action=close-detail");
    return true;
  }

  int8_t hit = hitTestBlip_(tx, ty);
  if (hit < 0) hit = hitTestRow_(tx, ty);
  if (hit >= 0) {
    selectAircraft_(hit);
    Logger::info("touch", where + " action=detail icao=" + String(selectedIcao_));
    return true;
  }

  return false;
}

int8_t RadarDashboard::hitTestBlip_(int16_t tx, int16_t ty) const {
  int8_t best = -1;
  long bestD = (long)kBlipHitRadius * kBlipHitRadius;
  for (uint8_t i = 0; i < snap_.count; i++) {
    if (blipX_[i] < 0) continue;
    long dx = tx - (kScopeX + blipX_[i]);
    long dy = ty - (kScopeY + blipY_[i]);
    long d = dx * dx + dy * dy;
    if (d <= bestD) {
      bestD = d;
      best = (int8_t)i;
    }
  }
  return best;
}

int8_t RadarDashboard::hitTestRow_(int16_t tx, int16_t ty) const {
  if (tx < kPanelX || tx > kPanelX + kPanelW) return -1;
  uint8_t rows = snap_.count < kMaxRows ? snap_.count : kMaxRows;
  for (uint8_t i = 0; i < rows; i++) {
    int16_t y = kListTop + i * kRowH;
    if (ty >= y - kRowHitPadY && ty <= y + kRowH + kRowHitPadY) return (int8_t)i;
  }
  return -1;
}

void RadarDashboard::cycleRange_() {
  uint8_t idx = kRangeStepCount - 1;
  for (uint8_t i = 0; i < kRangeStepCount; i++) {
    if (kRangeSteps[i] == rangeRingKm_) {
      idx = i;
      break;
    }
  }
  rangeRingKm_ = kRangeSteps[(idx + 1) % kRangeStepCount];
}

// No markRows() here on purpose: the tabs live inside the header band and the
// only caller (drawHeader_) marks rows 0..kHeaderH. Do not call this from
// anywhere else without marking.
void RadarDashboard::drawScreenTabs_(Arduino_GFX *g) {
  if (!g) return;

  // Tab well: groups the five tabs into one control instead of five chips
  // floating on the header fill.
  int16_t wellW = kTabGap * ((int16_t)kRadarScreenCount - 1) + 12;
  for (uint8_t i = 0; i < (uint8_t)kRadarScreenCount; i++) {
    wellW += screenTabWidthFor((RadarScreen)i);
  }
  g->fillRoundRect(kTabWellX, kTabWellY, wellW, kTabWellH, 10, kBg);
  g->drawRoundRect(kTabWellX, kTabWellY, wellW, kTabWellH, 10, kLine);

  int16_t x = kTabsX;
  for (uint8_t i = 0; i < (uint8_t)kRadarScreenCount; i++) {
    RadarScreen tab = (RadarScreen)i;
    int16_t w = screenTabWidthFor(tab);
    const char *label = screenTabLabelFor(tab);
    bool active = tab == screen_;
    uint16_t fill = active ? kAccent : kBg;
    uint16_t line = active ? kAccent : kLine;
    uint16_t fg = active ? kBg : kTextMut;

    g->fillRoundRect(x, kTabsY, w, kTabH, 7, fill);
    g->drawRoundRect(x, kTabsY, w, kTabH, 7, line);

    int16_t bx, by;
    uint16_t bw, bh;
    g->setFont(fontS());
    g->setTextSize(1);
    g->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
    int16_t ty = kTabsY + (kTabH - (int16_t)bh) / 2;
    text(g, x + w / 2, ty, label, fontS(), fg, kCenter);

    x += w + kTabGap;
  }
}

// One header for every screen. The band is identical throughout - fill, accent
// underline, title block, tab well, and a right-aligned pair of pills - and the
// screen only chooses what the left-hand pill is: the radar screen makes it a
// tappable RANGE control and records its rect for isRangeHit_, the world screens
// make it a passive page counter. Title and subtitle come from screen_ either
// way, so there is nothing left to pass in.
void RadarDashboard::drawHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  bool radar = (screen_ == kRadarScreen);

  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);

  // Only the radar screen wears the tower icon; the world titles start at the
  // margin the icon would otherwise occupy.
  int16_t titleX = 20;
  if (radar) {
    towerIcon(g, 20, 14, kAccent);
    titleX = 52;
  }
  text(g, titleX, 10, screenTitleFor(screen_), fontL(), kTextHi, kLeft);
  text(g, titleX, 34, screenSubtitleFor(screen_), fontS(), kTextMut, kLeft);
  drawScreenTabs_(g);

  char leadBuf[20];
  uint16_t leadText, leadFill;
  if (radar) {
    snprintf(leadBuf, sizeof(leadBuf), "RANGE %ukm >", (unsigned)rangeRingKm_);
    leadText = kBg;
    leadFill = kAccent;
  } else {
    snprintf(leadBuf, sizeof(leadBuf), "%u/%u", (unsigned)((uint8_t)screen_ + 1),
             (unsigned)kRadarScreenCount);
    leadText = kTextHi;
    leadFill = kSurfaceHi;
  }

  const char *linkLabel = USE_WIFI ? "LIVE" : "MOCK";
  uint16_t linkFill = USE_WIFI ? kGreen : kSurfaceHi;
  uint16_t linkText = USE_WIFI ? kBg : kTextMut;

  auto pillW = [&](const char *s) { return (int16_t)(textWidth(g, s, fontS()) + 24); };
  int16_t gap = 10;
  int16_t total = pillW(leadBuf) + gap + pillW(linkLabel);
  int16_t x = kScreenW - 16 - total;
  int16_t y = 14;

  int16_t leadW = pill(g, x, y, leadBuf, fontS(), leadText, leadFill);
  // Left stale on the world screens, exactly as before: isRangeHit_ is only
  // consulted while the radar screen is up.
  if (radar) {
    rangePillX_ = x;
    rangePillW_ = leadW;
  }
  x += leadW + gap;
  pill(g, x, y, linkLabel, fontS(), linkText, linkFill);
  markRows(0, kHeaderH);
}

// Instrument bay: the static frame the scope disc sits inside. Drawn once per
// full repaint - the disc itself arrives by blit.
void RadarDashboard::drawBay_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, kBayX, kBayY, kBayW, kBayH, 14, kBg, 1, kLine);
  text(g, kBayInX, kBayY + 10, "AIRSPACE SCOPE", fontS(), kAccent, kLeft);
  text(g, kBayInX + kBayInW, kBayY + 10, "N-UP", fontS(), kTextMut, kRight);
  g->drawFastHLine(kBayInX, kBayY + 28, kBayInW, kLine);
  markRows(kBayY, kBayH);
}

// The altitude colour code is meaningless without this. One ramp feeds the
// blips, the list dots, the detail-card border and these swatches.
void RadarDashboard::drawLegend_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kBayInX, kLegendY, kBayInW, kLegendH, kBg);
  text(g, kBayInX, kLegendY + 7, "ALT BAND", fontS(), kTextMut, kLeft);
  int16_t x = kBayInX + 78;
  for (uint8_t i = 0; i < RadarScope::altBandCount(); i++) {
    const AltBand &b = RadarScope::altBandAt(i);
    g->fillRect(x, kLegendY + 8, 10, 10, b.color);
    g->drawRect(x, kLegendY + 8, 10, 10, kLine);
    text(g, x + 15, kLegendY + 6, b.label, fontS(), kTextMut, kLeft);
    x += 15 + textWidth(g, b.label, fontS()) + 16;
  }
  markRows(kLegendY, kLegendH);
}

// Range ruler: a drawn scale reads faster than another line of text, and it
// makes the range-ring spacing on the disc concrete.
void RadarDashboard::drawRuler_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kBayInX, kRulerY, kBayInW, kRulerH, kBg);

  const int16_t rw = 268;
  const int16_t ry = kRulerY + 8;
  g->drawFastHLine(kBayInX, ry, rw, kLine);
  for (uint8_t i = 0; i <= 5; i++) {
    int16_t tx = kBayInX + rw * i / 5;
    g->drawFastVLine(tx, ry - 4, 9, (i == 0 || i == 5) ? kAccent : kLine);
    char lbl[8];
    if (i == 5) {
      snprintf(lbl, sizeof(lbl), "%ukm", (unsigned)rangeRingKm_);
    } else {
      snprintf(lbl, sizeof(lbl), "%u", (unsigned)(rangeRingKm_ * i / 5));
    }
    text(g, tx, ry + 10, lbl, fontS(), kTextMut, i == 0 ? kLeft : (i == 5 ? kRight : kCenter));
  }

  char rev[24];
  snprintf(rev, sizeof(rev), "%.1f s/REV", kSweepMsPerRev / 1000.0f);
  text(g, kBayInX + kBayInW, kRulerY + 12, rev, fontS(), kTextMut, kRight);
  markRows(kRulerY, kRulerH);
}

// ---------------------------------------------------------------------------
// Boot splash. Full-screen by design: a title/checklist column on the left and
// a large sweeping disc on the right, rather than a small disc floating in the
// middle of an otherwise empty panel. Drawn straight to the panel framebuffer
// (not the scope canvas) precisely so the disc is not capped at 360 px, and
// driven from tick() so setup() is never blocked.
// ---------------------------------------------------------------------------
namespace {
constexpr int16_t kSplashCx = 744;
constexpr int16_t kSplashCy = 302;
constexpr int16_t kSplashR = 208;
constexpr int16_t kSplashBezel = 16;
constexpr int16_t kSplashBoxR = kSplashR + kSplashBezel + 4;  // redraw bbox radius
constexpr int16_t kSplashColX = 52;
constexpr int16_t kSplashBarX = 52;
constexpr int16_t kSplashBarY = 512;
constexpr int16_t kSplashBarW = 408;
constexpr int16_t kSplashBarH = 20;
constexpr int16_t kSplashPctY = 480;

const char *const kSplashSteps[] = {
    "DISPLAY BRING-UP", "SCOPE BUFFER", "TOUCH CONTROLLER",
    "NETWORK LINK",     "TIME SYNC",    "AIRCRAFT FEED",
};
constexpr uint8_t kSplashStepCount = sizeof(kSplashSteps) / sizeof(kSplashSteps[0]);
constexpr int16_t kSplashStepY = 210;
constexpr int16_t kSplashStepPitch = 36;

const uint16_t kSplashGlow = rgb(0, 235, 255);
const uint16_t kSplashDim = rgb(0, 68, 82);
const uint16_t kSplashGreen = rgb(96, 255, 70);
}  // namespace

void RadarDashboard::drawIntroStatic_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  g->fillScreen(kBg);
  for (int16_t y = 0; y < kScreenH; y += 14) {
    g->drawFastHLine(0, y, kScreenW, rgb(0, 18, 22));
  }
  g->drawRect(20, 18, kScreenW - 40, kScreenH - 36, kSplashDim);
  g->drawRect(26, 24, kScreenW - 52, kScreenH - 48, rgb(0, 36, 42));

  text(g, kSplashColX, 44, "ADS-B RADAR", fontXL(), kTextHi, kLeft);
  text(g, kSplashColX, 108, "LIVE AIRCRAFT TRACKER", fontM(), kSplashGlow, kLeft);
  text(g, kSplashColX, 142, "INITIALIZING AIRSPACE SCAN", fontS(), kTextMut, kLeft);
  g->drawFastHLine(kSplashColX, 178, kSplashBarW, kSplashDim);

  for (uint8_t i = 0; i < kSplashStepCount; i++) {
    text(g, kSplashColX + 26, kSplashStepY + i * kSplashStepPitch, kSplashSteps[i], fontS(),
         kTextMut, kLeft);
  }

  g->drawRoundRect(kSplashBarX, kSplashBarY, kSplashBarW, kSplashBarH, 8, kSplashDim);
  text(g, kSplashColX, 548, "LOCKING RANGE RINGS | SYNCING AIRCRAFT FEED", fontS(), kTextMut,
       kLeft);
  markRows(0, kScreenH);
}

void RadarDashboard::drawIntroFrame_(uint32_t elapsedMs) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  float progress = (float)elapsedMs / (float)kIntroSplashMs;
  if (progress > 1.0f) progress = 1.0f;
  const int16_t cx = kSplashCx, cy = kSplashCy, rMax = kSplashR;
  float sweep = progress * 1080.0f;
  float sweepRad = (sweep - 90.0f) * (float)M_PI / 180.0f;

  // Whole-disc recompose. There is no offscreen buffer this large, but the
  // frame-to-frame delta is tiny (rings are static, only the sweep moves), so a
  // scanline caught mid-write shows a blend of two near-identical frames.
  g->fillRect(cx - kSplashBoxR, cy - kSplashBoxR, kSplashBoxR * 2, kSplashBoxR * 2, kBg);

  for (uint8_t i = 1; i <= 4; i++) {
    g->drawCircle(cx, cy, rMax * i / 4, i == 4 ? kSplashDim : rgb(18, 74, 20));
  }
  g->drawCircle(cx, cy, rMax + kSplashBezel, kSplashDim);
  g->drawFastHLine(cx - rMax - 22, cy, rMax * 2 + 44, rgb(18, 74, 20));
  g->drawFastVLine(cx, cy - rMax - 22, rMax * 2 + 44, rgb(18, 74, 20));
  for (int16_t deg = 0; deg < 360; deg += 15) {
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
    float ca = cosf(rad), sa = sinf(rad);
    if (deg % 30 == 0) {
      g->drawLine(cx + (int16_t)(ca * rMax), cy + (int16_t)(sa * rMax),
                  cx + (int16_t)(ca * (rMax + kSplashBezel)),
                  cy + (int16_t)(sa * (rMax + kSplashBezel)), kSplashDim);
      g->drawLine(cx, cy, cx + (int16_t)(ca * rMax), cy + (int16_t)(sa * rMax), rgb(0, 44, 36));
    } else {
      g->drawLine(cx + (int16_t)(ca * (rMax + 8)), cy + (int16_t)(sa * (rMax + 8)),
                  cx + (int16_t)(ca * (rMax + kSplashBezel)),
                  cy + (int16_t)(sa * (rMax + kSplashBezel)), rgb(0, 44, 36));
    }
  }

  for (int8_t tail = 14; tail >= 0; tail--) {
    float rad = (sweep - 90.0f - tail * 4.0f) * (float)M_PI / 180.0f;
    uint16_t col = tail == 0 ? kSplashGlow : rgb(0, 126 - tail * 7, 102 - tail * 5);
    g->drawLine(cx, cy, cx + (int16_t)(cosf(rad) * rMax), cy + (int16_t)(sinf(rad) * rMax), col);
  }
  g->fillCircle(cx, cy, 6, kSplashGreen);
  g->drawCircle(cx, cy, 13, kSplashDim);

  static const int16_t kBlipR[] = {62, 118, 156, 182, 96};
  static const int16_t kBlipA[] = {34, 138, 224, 304, 276};
  for (uint8_t i = 0; i < 5; i++) {
    float a = (float)(kBlipA[i] + (elapsedMs / (38 + i * 7))) * (float)M_PI / 180.0f;
    int16_t x = cx + (int16_t)(cosf(a) * kBlipR[i]);
    int16_t y = cy + (int16_t)(sinf(a) * kBlipR[i]);
    uint16_t col = (i % 2 == 0) ? kSplashGreen : kAmber;
    g->fillTriangle(x, y - 9, x - 8, y + 8, x + 8, y + 8, col);
    g->drawCircle(x, y, 14, rgb(0, 92, 60));
  }

  g->fillCircle(cx + (int16_t)(cosf(sweepRad) * (rMax + kSplashBezel / 2)),
                cy + (int16_t)(sinf(sweepRad) * (rMax + kSplashBezel / 2)), 5, kSplashGlow);
  text(g, cx, cy - rMax - 34, "N", fontS(), kSplashGreen, kCenter);
  text(g, cx, cy + rMax + 20, "S", fontS(), kSplashDim, kCenter);
  text(g, cx + rMax + 22, cy - 7, "E", fontS(), kSplashDim, kLeft);
  text(g, cx - rMax - 22, cy - 7, "W", fontS(), kSplashDim, kRight);
  markRows(cy - kSplashBoxR, kSplashBoxR * 2);

  // Boot checklist lights up as the scan progresses.
  for (uint8_t i = 0; i < kSplashStepCount; i++) {
    bool done = progress > (float)(i + 1) / (float)kSplashStepCount;
    statusDot(g, kSplashColX + 8, kSplashStepY + i * kSplashStepPitch + 7, 5,
              done ? kSplashGreen : kSplashDim);
  }
  markRows(kSplashStepY, kSplashStepCount * kSplashStepPitch);

  char pct[24];
  snprintf(pct, sizeof(pct), "SCAN %u%%", (unsigned)(progress * 100.0f));
  g->fillRect(kSplashColX, kSplashPctY, 240, 26, kBg);
  text(g, kSplashColX, kSplashPctY, pct, fontL(), kSplashGreen, kLeft);
  markRows(kSplashPctY, 26);

  int16_t fillW = (int16_t)((kSplashBarW - 4) * progress);
  g->fillRect(kSplashBarX + 2, kSplashBarY + 2, kSplashBarW - 4, kSplashBarH - 4, kBg);
  if (fillW > 4) {
    g->fillRoundRect(kSplashBarX + 2, kSplashBarY + 2, fillW, kSplashBarH - 4, 6, kSplashGlow);
  }
  markRows(kSplashBarY, kSplashBarH);
}

// ---------------------------------------------------------------------------
// Right-hand data column
// ---------------------------------------------------------------------------

void RadarDashboard::drawList_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  // The count pill overhangs kListY by 4 px, so the clear (and the flush range
  // below) start there too - a painter that draws outside the rows it marks
  // leaves those rows stale on the panel forever.
  g->fillRect(kPanelX, kListY - 4, kPanelW, kListH + 4, kBg);

  text(g, kPanelX, kListY + 2, "CONTACTS", fontL(), kTextHi, kLeft);
  char cap[16];
  snprintf(cap, sizeof(cap), "%u/%u", (unsigned)snap_.count, (unsigned)kMaxContacts);
  bool capped = snap_.count >= kMaxContacts;  // the store is dropping distant traffic
  pill(g, kPanelX + textWidth(g, "CONTACTS", fontL()) + 14, kListY - 4, cap, fontS(),
       capped ? kBg : kAccent, capped ? kAmber : kSurfaceHi);
  text(g, kPanelX + kPanelW, kListY + 8, "NEAREST FIRST", fontS(), kTextMut, kRight);
  g->drawFastHLine(kPanelX, kListY + kListHeadH, kPanelW, kLine);

  uint8_t rows = snap_.count < kMaxRows ? snap_.count : kMaxRows;
  for (uint8_t i = 0; i < rows; i++) {
    int16_t y = kListTop + i * kRowH;
    const Aircraft &a = snap_.ac[i];
    uint16_t band = RadarScope::altBandColor(a);
    bool sel = ((int8_t)i == selectedRow_);

    if (sel) {
      g->fillRect(kPanelX, y - 2, kPanelW, kRowH - 2, kSurfaceHi);
      g->fillRect(kPanelX, y - 2, 4, kRowH - 2, band);  // band-coloured spine
      RadarScope::arrow(g, kPanelX + kPanelW - 8, y + 17, 270.0f, 7.0f, kAccent);
    }

    char rank[4];
    snprintf(rank, sizeof(rank), "%u", (unsigned)(i + 1));
    text(g, kPanelX + 12, y + 12, rank, fontS(), kTextMut, kLeft);
    statusDot(g, kPanelX + 34, y + 15, 5, band);
    text(g, kPanelX + 48, y, a.callsign[0] ? a.callsign : "UNKNOWN", fontL(), kTextHi, kLeft);

    char altS[16], sub[40];
    fmtAlt(altS, sizeof(altS), a);
    snprintf(sub, sizeof(sub), "%s   %.0f kt", altS, a.groundSpeedKt);
    text(g, kPanelX + 48, y + 21, sub, fontS(), (a.haveAlt || a.onGround) ? kTextMut : kRed,
         kLeft);

    text(g, kPanelX + 206, y + 2, a.type[0] ? a.type : "----", fontS(), kTextHi, kLeft);
    const char *cat = categoryLabel(a.category);
    if (cat[0]) text(g, kPanelX + 206, y + 21, cat, fontS(), kTextMut, kLeft);

    // Distance: big number, small unit - reads at a glance from across a desk.
    char dist[10];
    snprintf(dist, sizeof(dist), "%.1f", a.distanceKm);
    int16_t dRight = kPanelX + kPanelW - 62;
    int16_t unitW = textWidth(g, "km", fontS());
    text(g, dRight, y + 9, "km", fontS(), kTextMut, kRight);
    text(g, dRight - unitW - 5, y + 2, dist, fontL(), kTextHi, kRight);

    // Bearing: drawn arrow + digits. The vendored fonts have no degree glyph.
    RadarScope::arrow(g, kPanelX + kPanelW - 42, y + 18, a.bearingDeg, 8.0f, kTextMut);
    char brg[8];
    snprintf(brg, sizeof(brg), "%03d", bearing360(a.bearingDeg));
    text(g, kPanelX + kPanelW - 8, y + 11, brg, fontS(), kTextMut, kRight);

    if (!sel && i + 1 < rows) {
      g->drawFastHLine(kPanelX + 12, y + kRowH - 4, kPanelW - 24, kLine);
    }
  }

  if (snap_.count == 0) {
    text(g, kPanelX + kPanelW / 2, kListTop + 40, "NO AIRCRAFT IN RANGE", fontL(), kTextMut,
         kCenter);
    char hint[48];
    snprintf(hint, sizeof(hint), "range %u km - tap RANGE to widen", (unsigned)rangeRingKm_);
    text(g, kPanelX + kPanelW / 2, kListTop + 72, hint, fontS(), kTextMut, kCenter);
  }
  markRows(kListY - 4, kListH + 4);
}

// Feed health. Surfaces totalSeen and generatedMs, which the snapshot has
// always carried and nothing ever displayed.
void RadarDashboard::drawFeedSpark_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, kPanelX, kFeedY, kPanelW, kFeedH, 10, kSurface);
  text(g, kPanelX + 16, kFeedY + 8, "FEED HEALTH", fontS(), kAccent, kLeft);
  if (histCount_ >= 2) {
    sparkline(g, kPanelX + 16, kFeedY + 32, 200, 42, contactHist_, histCount_, histTail_, 0.0f,
              0.0f, kAccent, rgb(0x0A, 0x3A, 0x40));
  } else {
    text(g, kPanelX + 16, kFeedY + 46, "sampling...", fontS(), kTextMut, kLeft);
  }
  g->drawFastVLine(kPanelX + 232, kFeedY + 16, 58, kLine);
  markRows(kFeedY, kFeedH);
}

void RadarDashboard::drawFeedStats_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kFeedStatsX - 6, kFeedStatsY - 6, kPanelW - (kFeedStatsX - kPanelX) - 10,
              kFeedStatsH + 8, kSurface);

  auto row = [&](int16_t y, const char *label, const char *value, uint16_t color) {
    text(g, kFeedStatsX, y, label, fontS(), kTextMut, kLeft);
    text(g, kPanelX + kPanelW - 16, y, value, fontS(), color, kRight);
  };

  char buf[24];
  snprintf(buf, sizeof(buf), "%u", (unsigned)snap_.count);
  row(kFeedStatsY, "TRACKED", buf, snap_.count >= kMaxContacts ? kAmber : kTextHi);

  snprintf(buf, sizeof(buf), "%u", (unsigned)snap_.totalSeen);
  row(kFeedStatsY + 20, "RAW SEEN", buf, kTextHi);

  uint32_t ageS = snap_.generatedMs ? (millis() - snap_.generatedMs) / 1000UL : 0;
  uint16_t ageColor = kGreen;
  if (!snap_.generatedMs) {
    ageColor = kRed;
  } else if (ageS >= 30) {
    ageColor = kRed;
  } else if (ageS >= 10) {
    ageColor = kAmber;
  }
  if (snap_.generatedMs) {
    snprintf(buf, sizeof(buf), "%us", (unsigned)ageS);
  } else {
    snprintf(buf, sizeof(buf), "no data");
  }
  row(kFeedStatsY + 40, "AGE", buf, ageColor);
  markRows(kFeedStatsY - 6, kFeedStatsH + 8);
}

// Station identity + clock, merged into one card. They used to be two
// half-empty blocks eating 182 px between them.
void RadarDashboard::drawStation_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, kPanelX, kStationY, kPanelW, kStationH, 10, kSurface);
  text(g, kPanelX + 16, kStationY + 10, "STATION", fontS(), kAccent, kLeft);
  text(g, kPanelX + 16, kStationY + 30, fit(g, String(ADSB_SITE_NAME), fontL(), 210).c_str(),
       fontL(), kTextHi, kLeft);
  char c[40];
  snprintf(c, sizeof(c), "%.4f, %.4f", (double)ADSB_HOME_LAT, (double)ADSB_HOME_LON);
  text(g, kPanelX + 16, kStationY + 54, c, fontS(), kTextMut, kLeft);
  g->drawFastVLine(kPanelX + 236, kStationY + 14, kStationH - 28, kLine);
  text(g, kPanelX + kPanelW - 16, kStationY + 10, "LOCAL TIME", fontS(), kTextMut, kRight);
  markRows(kStationY, kStationH);
}

// Digits only. The old clock cleared a 488x118 block once a second, which was a
// visible 1 Hz flash on a directly-scanned panel.
void RadarDashboard::drawClock_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kClockX, kClockY, kClockW, kClockH, kSurface);

  struct tm t;
  char buf[12];
  if (getLocalTime(&t, 5)) {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    strcpy(buf, "--:--:--");
  }
  text(g, kClockX + kClockW, kClockY, buf, fontXL(), kGreen, kRight);
  markRows(kClockY, kClockH);
}

// Footer chrome shared by the radar and world screens: the bar fill and its top
// rule, marking the whole band. `slotDividers` adds the radar footer's four-slot
// separators - they sit in the gaps between the slot rects drawFooter_ clears,
// which is why they can be painted once here and survive every content repaint.
// The status dot is NOT chrome: the radar one is inside a slot rect and would be
// erased by the next drawFooter_.
void RadarDashboard::drawFooterChrome_(bool slotDividers) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, kLine);
  if (slotDividers) {
    for (uint8_t i = 1; i < kFooterSlots; i++) {
      g->drawFastVLine(16 + i * 248 - 8, kFooterSlotY + 4, kFooterSlotH - 8, kLine);
    }
  }
  markRows(kFooterY, kFooterH);
}

// Only the four slot rects are cleared, never the full 1024x52 bar.
void RadarDashboard::drawFooter_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const int16_t slotW = 232;
  for (uint8_t i = 0; i < kFooterSlots; i++) {
    g->fillRect(16 + i * 248, kFooterSlotY, slotW, kFooterSlotH, kSurface);
  }

  statusDot(g, 24, kFooterSlotY + 18, 5, USE_WIFI ? kGreen : kAmber);
  text(g, 40, kFooterSlotY + 10, fit(g, String(snap_.source ? snap_.source : "-"), fontS(), 190)
                                     .c_str(),
       fontS(), kTextHi, kLeft);

  char buf[40];
  snprintf(buf, sizeof(buf), "RANGE %u km", (unsigned)rangeRingKm_);
  text(g, 264, kFooterSlotY + 10, buf, fontS(), kTextMut, kLeft);

  snprintf(buf, sizeof(buf), "%u TRACKED / %u SEEN", (unsigned)snap_.count,
           (unsigned)snap_.totalSeen);
  text(g, 512, kFooterSlotY + 10, buf, fontS(), kTextMut, kLeft);

  text(g, 992, kFooterSlotY + 10, "TAP A BLIP FOR DETAIL", fontS(), kTextMut, kRight);
  markRows(kFooterSlotY, kFooterSlotH);
}

// ---------------------------------------------------------------------------
// Content signatures. Every value is quantized to exactly what gets printed,
// otherwise the hash churns on float noise below the printed precision and the
// region repaints forever.
// ---------------------------------------------------------------------------

uint32_t RadarDashboard::headerSignature_() const {
  return (uint32_t)rangeRingKm_ | ((uint32_t)screen_ << 16);
}

uint32_t RadarDashboard::listSignature_() const {
  uint32_t h = kHashSeed;
  h = hashBytes(h, &snap_.count, sizeof(snap_.count));
  h = hashBytes(h, &selectedRow_, sizeof(selectedRow_));
  h = hashBytes(h, &rangeRingKm_, sizeof(rangeRingKm_));  // the empty-state hint
  uint8_t rows = snap_.count < kMaxRows ? snap_.count : kMaxRows;
  for (uint8_t i = 0; i < rows; i++) {
    const Aircraft &a = snap_.ac[i];
    h = hashBytes(h, a.callsign, sizeof(a.callsign));
    h = hashBytes(h, a.type, sizeof(a.type));
    h = hashBytes(h, a.category, sizeof(a.category));
    int32_t alt = (a.haveAlt && !a.onGround) ? a.altFt : (a.onGround ? INT32_MIN : INT32_MAX);
    int32_t kt = (int32_t)(a.groundSpeedKt + 0.5f);
    int32_t km = (int32_t)(a.distanceKm * 10.0f + 0.5f);  // drawn as "%.1f"
    int32_t brg = bearing360(a.bearingDeg);
    h = hashBytes(h, &alt, sizeof(alt));
    h = hashBytes(h, &kt, sizeof(kt));
    h = hashBytes(h, &km, sizeof(km));
    h = hashBytes(h, &brg, sizeof(brg));
  }
  return h;
}

uint32_t RadarDashboard::footerSignature_() const {
  uint32_t h = kHashSeed;
  h = hashBytes(h, &snap_.count, sizeof(snap_.count));
  h = hashBytes(h, &snap_.totalSeen, sizeof(snap_.totalSeen));
  h = hashBytes(h, &rangeRingKm_, sizeof(rangeRingKm_));
  const char *src = snap_.source ? snap_.source : "-";
  return hashBytes(h, src, strlen(src));
}

uint32_t RadarDashboard::feedSignature_() const {
  uint32_t h = kHashSeed;
  h = hashBytes(h, &snap_.count, sizeof(snap_.count));
  h = hashBytes(h, &snap_.totalSeen, sizeof(snap_.totalSeen));
  uint32_t ageS = snap_.generatedMs ? (millis() - snap_.generatedMs) / 1000UL : 0xFFFFFFFFu;
  return hashBytes(h, &ageS, sizeof(ageS));
}

uint32_t RadarDashboard::clockSignature_() const {
  struct tm t;
  char buf[12];
  if (getLocalTime(&t, 5)) {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    strcpy(buf, "--:--:--");
  }
  return hashBytes(kHashSeed, buf, 8);
}


void RadarDashboard::drawWorldFooter_(bool valid, unsigned long ms, const String &error) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  drawFooterChrome_(/*slotDividers=*/false);

  bool stale = !valid || (ms == 0) || ((millis() - ms) > kWorldStaleMs);
  uint16_t dot = valid ? (stale ? kAmber : kGreen) : kRed;
  statusDot(g, 20, kFooterY + 26, 5, dot);

  String left = valid ? ("updated " + feedAge(ms)) : "waiting for feed";
  if (error.length() > 0) {
    left += " | ";
    left += error;
  }
  text(g, 38, kFooterY + 18, fit(g, left, fontS(), 560).c_str(), fontS(),
       valid ? kTextHi : kTextMut, kLeft);

  text(g, kScreenW - 16, kFooterY + 18, "tabs", fontS(), kTextMut, kRight);
  markRows(kFooterY, kFooterH);  // same band the chrome marked; kept so this
                                 // painter stands on its own if that changes
}

// Refreshes only the "updated Ns ago" line. The 30 s world-feed gate used to
// call drawWorldScreen_(), i.e. a full-screen fillScreen, for this one string.
void RadarDashboard::drawWorldFooterForScreen_() {
  switch (screen_) {
    case kWeatherScreen:
      drawWorldFooter_(world_.weatherValid, world_.weatherMs, world_.weatherError);
      break;
    case kQuakeScreen:
      drawWorldFooter_(world_.quakeValid, world_.quakeMs, world_.quakeError);
      break;
    case kAuroraScreen:
      drawWorldFooter_(world_.auroraValid, world_.auroraMs, world_.auroraError);
      break;
    case kAirScreen:
      drawWorldFooter_(world_.airValid, world_.airMs, world_.airError);
      break;
    default:
      break;
  }
}

void RadarDashboard::drawWorldScreen_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  // This whole-screen mark is what covers the per-screen painters below - none
  // of drawWeatherScreen_/drawQuakeScreen_/drawAuroraScreen_/drawAirScreen_
  // marks its own rows, so they must only ever be reached through here.
  g->fillScreen(kBg);
  markRows(0, kScreenH);
  switch (screen_) {
    case kWeatherScreen:
      drawWeatherScreen_();
      break;
    case kQuakeScreen:
      drawQuakeScreen_();
      break;
    case kAuroraScreen:
      drawAuroraScreen_();
      break;
    case kAirScreen:
      drawAirScreen_();
      break;
    default:
      setScreen_(kRadarScreen);
      break;
  }
}

void RadarDashboard::drawWeatherScreen_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  drawHeader_();

  const WeatherData &w = world_.weather;
  uint16_t accent = world_.weatherValid ? kAccent : kLine;
  String temp = world_.weatherValid ? fmtFloat(w.tempC, 1, "C") : "--";
  String sub = world_.weatherValid ? w.condition : world_.weatherError;
  metricCard(g, kWorldLeft, kWorldTop + 24, 360, 192, "CURRENT", temp, sub, accent);

  panel(g, kWorldLeft + 392, kWorldTop + 24, kWorldW - 392, 192, 8, kSurface);
  text(g, kWorldLeft + 416, kWorldTop + 42, "LOCAL FORECAST", fontS(), kAccent, kLeft);
  text(g, kWorldLeft + 416, kWorldTop + 78, fit(g, ADSB_SITE_NAME, fontL(), 360).c_str(),
       fontL(), kTextHi, kLeft);
  char coords[48];
  snprintf(coords, sizeof(coords), "%.4f, %.4f", (double)ADSB_HOME_LAT, (double)ADSB_HOME_LON);
  text(g, kWorldLeft + 416, kWorldTop + 116, coords, fontM(), kTextMut, kLeft);
  String age = world_.weatherValid ? feedAge(world_.weatherMs) : "not loaded";
  text(g, kWorldLeft + 416, kWorldTop + 154, age.c_str(), fontS(), kTextMut, kLeft);

  int16_t y = kWorldTop + 252;
  int16_t gap = 16;
  int16_t cw = (kWorldW - 3 * gap) / 4;
  metricCard(g, kWorldLeft, y, cw, 132, "FEELS", fmtFloat(w.feelsC, 1, "C"), "", kGreen);
  metricCard(g, kWorldLeft + (cw + gap), y, cw, 132, "WIND", fmtFloat(w.windKt, 0, " kt"), "", kAccent);
  metricCard(g, kWorldLeft + 2 * (cw + gap), y, cw, 132, "HIGH", fmtFloat(w.hiC, 1, "C"), "", kAmber);
  metricCard(g, kWorldLeft + 3 * (cw + gap), y, cw, 132, "LOW", fmtFloat(w.loC, 1, "C"), "", kTextMut);

  float tempNorm = numberReady(w.tempC) ? ((w.tempC + 10.0f) / 50.0f) : 0.0f;
  text(g, kWorldLeft, kWorldTop + 416, "temperature band", fontS(), kTextMut, kLeft);
  hBar(g, kWorldLeft, kWorldTop + 444, kWorldW, 18, tempNorm, accent, kLine);
  drawWorldFooter_(world_.weatherValid, world_.weatherMs, world_.weatherError);
}

void RadarDashboard::drawQuakeScreen_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  drawHeader_();

  const QuakeData &q = world_.quake;
  uint16_t accent = quakeColor(q.mag);
  String mag = world_.quakeValid ? ("M" + String(q.mag, 1)) : "--";
  String place = world_.quakeValid ? q.place : world_.quakeError;
  metricCard(g, kWorldLeft, kWorldTop + 24, 430, 220, "LATEST M4.5+ EVENT", mag, place, accent);

  panel(g, kWorldLeft + 462, kWorldTop + 24, kWorldW - 462, 220, 8, kSurface);
  text(g, kWorldLeft + 486, kWorldTop + 48, "FEED SUMMARY", fontS(), kAmber, kLeft);
  String count = (q.count24h >= 0) ? String(q.count24h) + " events in 24h" : "-- events";
  text(g, kWorldLeft + 486, kWorldTop + 88, count.c_str(), fontL(), kTextHi, kLeft);
  String age = (q.ageMin >= 0) ? String(q.ageMin) + " minutes old" : "age unknown";
  text(g, kWorldLeft + 486, kWorldTop + 130, age.c_str(), fontM(), kTextMut, kLeft);
  String depth = numberReady(q.depthKm) ? ("depth " + String(q.depthKm, 1) + " km") : "depth --";
  text(g, kWorldLeft + 486, kWorldTop + 168, depth.c_str(), fontM(), kTextMut, kLeft);

  int16_t y = kWorldTop + 280;
  metricCard(g, kWorldLeft, y, 280, 144, "MAGNITUDE", world_.quakeValid ? String(q.mag, 1) : "--",
             "USGS reported", accent);
  metricCard(g, kWorldLeft + 312, y, 280, 144, "DEPTH", fmtFloat(q.depthKm, 1, " km"),
             "below surface", kAccent);
  metricCard(g, kWorldLeft + 624, y, 280, 144, "COUNT", (q.count24h >= 0) ? String(q.count24h) : "--",
             "past day", kAmber);

  float severity = numberReady(q.mag) ? ((q.mag - 4.5f) / 3.0f) : 0.0f;
  text(g, kWorldLeft, kWorldTop + 416, "severity scale", fontS(), kTextMut, kLeft);
  hBar(g, kWorldLeft, kWorldTop + 444, kWorldW, 18, severity, accent, kLine);
  drawWorldFooter_(world_.quakeValid, world_.quakeMs, world_.quakeError);
}

void RadarDashboard::drawAuroraScreen_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  drawHeader_();

  const AuroraData &a = world_.aurora;
  uint16_t accent = a.verdict == kAuroraLikely ? kGreen
                    : a.verdict == kAuroraWatch ? kAmber : kAccent;
  String kp = world_.auroraValid ? ("Kp " + String(a.kp, 1)) : "--";
  String verdict = a.verdict == kAuroraLikely ? "aurora likely"
                   : a.verdict == kAuroraWatch ? "aurora watch" : "quiet";
  if (!world_.auroraValid) verdict = world_.auroraError;
  metricCard(g, kWorldLeft, kWorldTop + 24, 430, 220, "PLANETARY INDEX", kp, verdict, accent);

  panel(g, kWorldLeft + 462, kWorldTop + 24, kWorldW - 462, 220, 8, kSurface);
  text(g, kWorldLeft + 486, kWorldTop + 48, "SPACE WEATHER", fontS(), accent, kLeft);
  text(g, kWorldLeft + 486, kWorldTop + 88, world_.auroraValid ? a.level.c_str() : "--",
       fontXL(), kTextHi, kLeft);
  char threshold[36];
  snprintf(threshold, sizeof(threshold), "local threshold Kp %d", ADSB_WORLD_KP_THRESHOLD);
  text(g, kWorldLeft + 486, kWorldTop + 146, threshold, fontM(), kTextMut, kLeft);
  const char *trend = a.trend > 0 ? "trend rising" : a.trend < 0 ? "trend falling" : "trend flat";
  text(g, kWorldLeft + 486, kWorldTop + 180, trend, fontM(), kTextMut, kLeft);

  float kpNorm = numberReady(a.kp) ? (a.kp / 9.0f) : 0.0f;
  int16_t y = kWorldTop + 286;
  panel(g, kWorldLeft, y, kWorldW, 144, 8, kSurface);
  text(g, kWorldLeft + 20, y + 18, "Kp intensity", fontS(), kTextMut, kLeft);
  hBar(g, kWorldLeft + 20, y + 64, kWorldW - 40, 24, kpNorm, accent, kLine);
  text(g, kWorldLeft + 20, y + 106, "0", fontS(), kTextMut, kLeft);
  text(g, kWorldLeft + kWorldW / 2, y + 106, "5 storm", fontS(), kTextMut, kCenter);
  text(g, kWorldLeft + kWorldW - 20, y + 106, "9", fontS(), kTextMut, kRight);

  if (world_.auroraValid) {
    int16_t ax = kWorldRight - 92;
    int16_t ay = kWorldTop + 146;
    if (a.trend > 0) {
      g->fillTriangle(ax, ay, ax - 16, ay + 30, ax + 16, ay + 30, kGreen);
    } else if (a.trend < 0) {
      g->fillTriangle(ax, ay + 30, ax - 16, ay, ax + 16, ay, kAmber);
    } else {
      g->fillRect(ax - 20, ay + 14, 40, 6, kTextMut);
    }
  }
  drawWorldFooter_(world_.auroraValid, world_.auroraMs, world_.auroraError);
}

void RadarDashboard::drawAirScreen_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  drawHeader_();

  const AirQualityData &a = world_.air;
  uint16_t accent = aqiColor(a.usAqi);
  String aqi = world_.airValid ? String(a.usAqi, 0) : "--";
  String category = world_.airValid ? a.category : world_.airError;
  metricCard(g, kWorldLeft, kWorldTop + 24, 360, 192, "US AQI", aqi, category, accent);

  panel(g, kWorldLeft + 392, kWorldTop + 24, kWorldW - 392, 192, 8, kSurface);
  text(g, kWorldLeft + 416, kWorldTop + 42, "HEALTH BAND", fontS(), accent, kLeft);
  text(g, kWorldLeft + 416, kWorldTop + 78,
       world_.airValid ? fit(g, a.category, fontL(), 360).c_str() : "waiting for feed",
       fontL(), world_.airValid ? kTextHi : kTextMut, kLeft);
  text(g, kWorldLeft + 416, kWorldTop + 118, "0-50 good  |  51-100 moderate  |  101+ unhealthy",
       fontS(), kTextMut, kLeft);
  float aqiNorm = numberReady(a.usAqi) ? (a.usAqi / 200.0f) : 0.0f;
  hBar(g, kWorldLeft + 416, kWorldTop + 154, kWorldW - 440, 18, aqiNorm, accent, kLine);

  int16_t y = kWorldTop + 252;
  int16_t gap = 16;
  int16_t cw = (kWorldW - 3 * gap) / 4;
  metricCard(g, kWorldLeft, y, cw, 132, "PM2.5", fmtFloat(a.pm25, 1), "micrograms/m3", kAccent);
  metricCard(g, kWorldLeft + (cw + gap), y, cw, 132, "PM10", fmtFloat(a.pm10, 1), "micrograms/m3", kAmber);
  metricCard(g, kWorldLeft + 2 * (cw + gap), y, cw, 132, "OZONE", fmtFloat(a.ozone, 0), "micrograms/m3", kGreen);
  metricCard(g, kWorldLeft + 3 * (cw + gap), y, cw, 132, "UV", fmtFloat(a.uvIndex, 1), "index", kTextMut);

  text(g, kWorldLeft, kWorldTop + 430, "air quality updates on a slow cadence", fontS(), kTextMut, kLeft);
  drawWorldFooter_(world_.airValid, world_.airMs, world_.airError);
}

#else  // display disabled: no-op so the sketch still runs Serial-only

void RadarDashboard::begin(uint16_t initialRangeKm) { rangeRingKm_ = initialRangeKm; }
void RadarDashboard::tick(AircraftStore &) {}
void RadarDashboard::setRangeKm(uint16_t km) { rangeRingKm_ = km; }
void RadarDashboard::setWorldFeeds(const WorldFeeds &feeds) { world_ = feeds; }

// Nothing to repaint and no selection to clear - just the state change.
void RadarDashboard::setScreen_(RadarScreen screen) {
  if (screen >= kRadarScreenCount) screen = kRadarScreen;
  screen_ = screen;
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
