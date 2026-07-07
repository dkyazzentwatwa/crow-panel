#include "RadarDashboard.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

using namespace Widgets;

namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kHeaderH = 56;
constexpr int16_t kScopeX = 76;
constexpr int16_t kScopeY = 122;
constexpr int16_t kScopeW = 360;  // small enough to fit fast internal SRAM
constexpr int16_t kScopeH = 360;
constexpr int16_t kPanelX = 520;
constexpr int16_t kPanelW = kScreenW - kPanelX - 16;  // 488
constexpr int16_t kListTop = 100;
constexpr int16_t kRowH = 40;
constexpr uint8_t kMaxRows = 6;
constexpr int16_t kLocY = 350;
constexpr int16_t kTimeY = 426;
constexpr int16_t kFooterY = 548;
constexpr int16_t kFooterH = 52;
constexpr int16_t kWorldTop = 76;
constexpr int16_t kWorldLeft = 48;
constexpr int16_t kWorldRight = kScreenW - 48;
constexpr int16_t kWorldW = kWorldRight - kWorldLeft;
constexpr uint32_t kWorldStaleMs = 45UL * 60UL * 1000UL;
constexpr float kDegPerMs = 360.0f / 4500.0f;  // ~4.5 s per revolution

uint16_t altColor(const Aircraft &a) {
  if (!a.haveAlt) return kRed;
  if (a.altFt >= 30000) return rgb(0, 180, 255);
  if (a.altFt >= 10000) return kGreen;
  return kAmber;
}

String fit(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String t = s;
  while (t.length() > 1 && textWidth(g, (t + "...").c_str(), font) > maxW) {
    t.remove(t.length() - 1);
  }
  return t + "...";
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
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "ADS-B Flight Radar") &&
           (CrowDisplay::canvas() != nullptr);
  if (!ready_) return;

  if (!scope_.begin(kScopeW, kScopeH)) {
    Logger::error("radar", "scope PSRAM alloc failed; running chrome-only");
  }
  Logger::info("radar", String("scope ") + kScopeW + "x" + kScopeH + " in " +
                            (scope_.bufferInternal() ? "internal-SRAM" : "PSRAM") + "; free int " +
                            String(ESP.getFreeHeap() / 1024) + "KB / psram " +
                            String(ESP.getFreePsram() / 1024) + "KB");
  configTzTime(ADSB_TZ, "pool.ntp.org", "time.nist.gov");

  memset(blipX_, 0xFF, sizeof(blipX_));  // -1
  memset(blipY_, 0xFF, sizeof(blipY_));
  memset(&snap_, 0, sizeof(snap_));
  lastFrameMs_ = millis();

  CrowDisplay::canvas()->fillScreen(kBg);
  drawHeader_();
  drawList_();
  drawLocation_();
  drawClock_();
  drawFooter_();
  screenDirty_ = false;
}

void RadarDashboard::setRangeKm(uint16_t km) { rangeRingKm_ = km; }

void RadarDashboard::setWorldFeeds(const WorldFeeds &feeds) {
  world_ = feeds;
  if (screen_ != kRadarScreen) screenDirty_ = true;
}

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

void RadarDashboard::setScreen_(RadarScreen screen) {
  if (screen >= kRadarScreenCount) screen = kRadarScreen;
  if (screen_ == screen && !screenDirty_) return;
  screen_ = screen;
  detailOpen_ = false;
  selectedIdx_ = -1;
  chromeSig_ = 0xFFFFFFFF;
  screenDirty_ = true;
  Logger::info("screen", String("show ") + screenNameFor(screen_));
}

void RadarDashboard::tick(AircraftStore &store) {
  if (!ready_) return;

  handleTouch_();

  if (screen_ != kRadarScreen) {
    if (screenDirty_ || worldRefreshGate_.ready()) {
      drawWorldScreen_();
      screenDirty_ = false;
    }
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
  if (selectedIdx_ >= (int8_t)snap_.count) {
    selectedIdx_ = -1;
    detailOpen_ = false;
  }

  uint32_t t0 = micros();
  if (screenDirty_) {
    CrowDisplay::canvas()->fillScreen(kBg);
    drawHeader_();
    drawList_();
    drawLocation_();
    drawClock_();
    drawFooter_();
    screenDirty_ = false;
  }
  scope_.render(snap_, sweepDeg_, selectedIdx_, blipX_, blipY_);
  blitScope_();
  frameAccumUs_ += (micros() - t0);
  frameCount_++;
  if (statGate_.ready() && frameCount_ > 0) {
    Logger::info("radar", "render " + String(frameAccumUs_ / frameCount_ / 1000.0f, 1) +
                              " ms/frame (~" + String(frameCount_ / 2) + " fps), contacts " +
                              String(snap_.count));
    frameAccumUs_ = 0;
    frameCount_ = 0;
  }

  // Repaint the header/list only when the structure (contacts, selection, range)
  // changes, and refresh the list on a slow tick so distances stay current; the
  // clock + footer tick once a second. This keeps the right panel from flashing
  // every frame while the scope animates.
  uint32_t sig = (uint32_t)snap_.count | ((uint32_t)(uint8_t)selectedIdx_ << 8) |
                 ((uint32_t)rangeRingKm_ << 16);
  bool changed = (sig != chromeSig_);
  if (changed) {
    chromeSig_ = sig;
    drawHeader_();
  }
  if (changed || listRefreshGate_.ready()) drawList_();
  if (clockGate_.ready()) {
    drawClock_();
    drawFooter_();
  }
  if (detailOpen_) drawDetail_();
}

void RadarDashboard::blitScope_() {
  uint16_t *fb = scope_.framebuffer();
  if (!fb) return;
  CrowDisplay::canvas()->draw16bitRGBBitmap(kScopeX, kScopeY, fb, scope_.width(), scope_.height());
}

void RadarDashboard::handleTouch_() {
  int16_t tx, ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  if (touched && !wasTouched_) {
    if (tx >= nextPillX_ && tx <= nextPillX_ + nextPillW_ && ty >= 8 && ty <= 46) {
      nextScreen();
    } else if (screen_ != kRadarScreen) {
      // World screens are read-only for v1; the header NEXT control advances.
    } else if (tx >= rangePillX_ && tx <= rangePillX_ + rangePillW_ && ty >= 8 && ty <= 46) {
      cycleRange_();
    } else if (detailOpen_) {
      bool inside = tx >= detailX_ && tx <= detailX_ + detailW_ && ty >= detailY_ &&
                    ty <= detailY_ + detailH_;
      if (!inside) {
        detailOpen_ = false;
        selectedIdx_ = -1;
      }
    } else {
      int8_t hit = hitTestBlip_(tx, ty);
      if (hit < 0) hit = hitTestRow_(tx, ty);
      if (hit >= 0) {
        selectedIdx_ = hit;
        detailOpen_ = true;
      }
    }
  }
  wasTouched_ = touched;
}

int8_t RadarDashboard::hitTestBlip_(int16_t tx, int16_t ty) const {
  int8_t best = -1;
  long bestD = 22 * 22;
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
    if (ty >= y - 2 && ty <= y + kRowH - 2) return (int8_t)i;
  }
  return -1;
}

void RadarDashboard::cycleRange_() {
  static const uint16_t steps[] = {20, 40, 60, 80, 100};
  uint8_t idx = 4;
  for (uint8_t i = 0; i < 5; i++) {
    if (steps[i] == rangeRingKm_) {
      idx = i;
      break;
    }
  }
  rangeRingKm_ = steps[(idx + 1) % 5];
}

void RadarDashboard::drawHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);
  text(g, 20, 10, "ADS-B RADAR", fontL(), kTextHi, kLeft);
  text(g, 20, 34, "LIVE AIRCRAFT TRACKER", fontS(), kTextMut, kLeft);

  const char *linkLabel = USE_WIFI ? "LIVE" : "MOCK";
  uint16_t linkFill = USE_WIFI ? kGreen : kSurfaceHi;
  uint16_t linkText = USE_WIFI ? kBg : kTextMut;

  char rangeBuf[16];
  snprintf(rangeBuf, sizeof(rangeBuf), "RANGE %ukm", (unsigned)rangeRingKm_);
  char cntBuf[20];
  snprintf(cntBuf, sizeof(cntBuf), "%u contacts", (unsigned)snap_.count);

  auto pillW = [&](const char *s) { return (int16_t)(textWidth(g, s, fontS()) + 24); };
  int16_t gap = 10;
  int16_t cntW = textWidth(g, cntBuf, fontS());
  int16_t total = cntW + 16 + pillW(rangeBuf) + gap + pillW("NEXT") + gap + pillW(linkLabel);
  int16_t x = kScreenW - 16 - total;
  int16_t y = 14;

  text(g, x, 20, cntBuf, fontS(), kTextMut, kLeft);
  x += cntW + 16;
  rangePillX_ = x;
  rangePillW_ = pill(g, x, y, rangeBuf, fontS(), kBg, kAccent);
  x += rangePillW_ + gap;
  nextPillX_ = x;
  nextPillW_ = pill(g, x, y, "NEXT", fontS(), kTextHi, kSurfaceHi);
  x += nextPillW_ + gap;
  pill(g, x, y, linkLabel, fontS(), linkText, linkFill);
}

void RadarDashboard::drawList_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kPanelX, 60, kPanelW, 290, kBg);
  text(g, kPanelX, 62, "DETECTED AIRCRAFT", fontL(), kTextHi, kLeft);
  text(g, kPanelX + kPanelW, 66, "nearest first", fontS(), kTextMut, kRight);

  uint8_t rows = snap_.count < kMaxRows ? snap_.count : kMaxRows;
  for (uint8_t i = 0; i < rows; i++) {
    int16_t y = kListTop + i * kRowH;
    const Aircraft &a = snap_.ac[i];
    if (i == selectedIdx_) g->fillRect(kPanelX, y - 2, kPanelW, kRowH - 2, kSurfaceHi);

    statusDot(g, kPanelX + 10, y + 12, 5, altColor(a));
    text(g, kPanelX + 26, y, a.callsign[0] ? a.callsign : "UNKNOWN", fontL(), kTextHi, kLeft);

    char sub[48];
    if (a.haveAlt) {
      snprintf(sub, sizeof(sub), "%s  %ldft  %.0fkt", a.type[0] ? a.type : "-", (long)a.altFt,
               a.groundSpeedKt);
    } else {
      snprintf(sub, sizeof(sub), "%s  --ft  %.0fkt", a.type[0] ? a.type : "-", a.groundSpeedKt);
    }
    text(g, kPanelX + 26, y + 20, sub, fontS(), kTextMut, kLeft);

    char dst[12];
    snprintf(dst, sizeof(dst), "%.0f km", a.distanceKm);
    text(g, kPanelX + kPanelW - 6, y, dst, fontS(), kTextHi, kRight);
    char brg[8];
    snprintf(brg, sizeof(brg), "%03d", (int)(a.bearingDeg + 0.5f) % 360);
    text(g, kPanelX + kPanelW - 6, y + 20, brg, fontS(), kTextMut, kRight);

    g->drawFastHLine(kPanelX, y + kRowH - 4, kPanelW, kLine);
  }
  if (snap_.count == 0) {
    text(g, kPanelX + kPanelW / 2, kListTop + 24, "NO AIRCRAFT IN RANGE", fontS(), kTextMut, kCenter);
  }
}

void RadarDashboard::drawLocation_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kPanelX, kLocY, kPanelW, 64, kBg);
  g->drawFastHLine(kPanelX, kLocY, kPanelW, kLine);
  text(g, kPanelX + kPanelW / 2, kLocY + 8, ADSB_SITE_NAME, fontL(), kAccent, kCenter);
  char c[40];
  snprintf(c, sizeof(c), "%.4f, %.4f", (double)ADSB_HOME_LAT, (double)ADSB_HOME_LON);
  text(g, kPanelX + kPanelW / 2, kLocY + 36, c, fontM(), kTextHi, kCenter);
}

void RadarDashboard::drawClock_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(kPanelX, kTimeY, kPanelW, 118, kBg);
  g->drawFastHLine(kPanelX, kTimeY, kPanelW, kLine);
  text(g, kPanelX, kTimeY + 12, "LOCAL TIME", fontS(), kTextMut, kLeft);

  struct tm t;
  char buf[12];
  if (getLocalTime(&t, 5)) {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    strcpy(buf, "--:--:--");
  }
  text(g, kPanelX + kPanelW / 2, kTimeY + 44, buf, fontXL(), kGreen, kCenter);
}

void RadarDashboard::drawFooter_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, kLine);

  statusDot(g, 20, kFooterY + 26, 5, USE_WIFI ? kGreen : kAmber);
  char src[48];
  snprintf(src, sizeof(src), "src: %s", snap_.source ? snap_.source : "-");
  text(g, 38, kFooterY + 18, src, fontS(), kTextHi, kLeft);

  text(g, kScreenW / 2, kFooterY + 18, "north-up", fontS(), kTextMut, kCenter);

  char sweep[16];
  snprintf(sweep, sizeof(sweep), "SWEEP %03d", ((int)sweepDeg_) % 360);
  text(g, kScreenW - 16, kFooterY + 18, sweep, fontS(), kTextMut, kRight);
}

void RadarDashboard::drawDetail_() {
  if (selectedIdx_ < 0 || selectedIdx_ >= (int8_t)snap_.count) return;
  const Aircraft &a = snap_.ac[selectedIdx_];
  Arduino_GFX *g = CrowDisplay::canvas();

  detailW_ = 360;
  detailH_ = 176;
  detailX_ = kScopeX + (kScopeW - detailW_) / 2;
  detailY_ = kScopeY + (kScopeH - detailH_) / 2;
  panel(g, detailX_, detailY_, detailW_, detailH_, 12, kSurface, 2, kAccent);

  int16_t x = detailX_ + 18;
  statusDot(g, x + 4, detailY_ + 24, 6, altColor(a));
  text(g, x + 20, detailY_ + 12, a.callsign[0] ? a.callsign : "UNKNOWN", fontXL(), kTextHi, kLeft);
  text(g, x, detailY_ + 52, a.icao, fontS(), kTextMut, kLeft);
  if (a.type[0]) text(g, detailX_ + detailW_ - 14, detailY_ + 52, a.type, fontS(), kTextMut, kRight);

  char l1[40], l2[48], l3[48];
  if (a.haveAlt) {
    snprintf(l1, sizeof(l1), "ALT   %ld ft", (long)a.altFt);
  } else {
    snprintf(l1, sizeof(l1), "ALT   %s", a.onGround ? "on ground" : "--");
  }
  snprintf(l2, sizeof(l2), "SPD   %.0f kt      TRK  %03d", a.groundSpeedKt,
           (int)(a.trackDeg + 0.5f) % 360);
  snprintf(l3, sizeof(l3), "RNG   %.1f km    BRG  %03d", a.distanceKm,
           (int)(a.bearingDeg + 0.5f) % 360);
  text(g, x, detailY_ + 82, l1, fontM(), kTextHi, kLeft);
  text(g, x, detailY_ + 110, l2, fontM(), kTextHi, kLeft);
  text(g, x, detailY_ + 138, l3, fontM(), kTextHi, kLeft);
  text(g, detailX_ + detailW_ - 14, detailY_ + detailH_ - 22, "tap outside to close", fontS(),
       kTextMut, kRight);
}

void RadarDashboard::drawWorldHeader_(const char *title, const char *subtitle) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);
  text(g, 20, 10, title, fontL(), kTextHi, kLeft);
  text(g, 20, 34, subtitle, fontS(), kTextMut, kLeft);

  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "%u/%u", (unsigned)((uint8_t)screen_ + 1),
           (unsigned)kRadarScreenCount);
  const char *linkLabel = USE_WIFI ? "LIVE" : "MOCK";
  uint16_t linkFill = USE_WIFI ? kGreen : kSurfaceHi;
  uint16_t linkText = USE_WIFI ? kBg : kTextMut;

  auto pillW = [&](const char *s) { return (int16_t)(textWidth(g, s, fontS()) + 24); };
  int16_t gap = 10;
  int16_t total = pillW(pageBuf) + gap + pillW("NEXT") + gap + pillW(linkLabel);
  int16_t x = kScreenW - 16 - total;
  int16_t y = 14;
  x += pill(g, x, y, pageBuf, fontS(), kTextHi, kSurfaceHi) + gap;
  nextPillX_ = x;
  nextPillW_ = pill(g, x, y, "NEXT", fontS(), kBg, kAccent);
  x += nextPillW_ + gap;
  pill(g, x, y, linkLabel, fontS(), linkText, linkFill);
}

void RadarDashboard::drawWorldFooter_(bool valid, unsigned long ms, const String &error) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, kLine);

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

  text(g, kScreenW / 2 + 160, kFooterY + 18, ADSB_SITE_NAME, fontS(), kTextMut, kCenter);
  text(g, kScreenW - 16, kFooterY + 18, "tap NEXT", fontS(), kTextMut, kRight);
}

void RadarDashboard::drawWorldScreen_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
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
  drawWorldHeader_(screenTitleFor(screen_), screenSubtitleFor(screen_));

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
  drawWorldHeader_(screenTitleFor(screen_), screenSubtitleFor(screen_));

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
  drawWorldHeader_(screenTitleFor(screen_), screenSubtitleFor(screen_));

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
  drawWorldHeader_(screenTitleFor(screen_), screenSubtitleFor(screen_));

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
void RadarDashboard::nextScreen() {
  screen_ = (RadarScreen)(((uint8_t)screen_ + 1) % (uint8_t)kRadarScreenCount);
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
    screen_ = kRadarScreen;
    return true;
  }
  if (n == "weather" || n == "wx") {
    screen_ = kWeatherScreen;
    return true;
  }
  if (n == "quake" || n == "quakes" || n == "earthquake" || n == "earthquakes") {
    screen_ = kQuakeScreen;
    return true;
  }
  if (n == "aurora" || n == "kp" || n == "space") {
    screen_ = kAuroraScreen;
    return true;
  }
  if (n == "air" || n == "aqi" || n == "airquality") {
    screen_ = kAirScreen;
    return true;
  }
  return false;
}
const char *RadarDashboard::screenName() const {
  switch (screen_) {
    case kRadarScreen: return "radar";
    case kWeatherScreen: return "weather";
    case kQuakeScreen: return "quake";
    case kAuroraScreen: return "aurora";
    case kAirScreen: return "air";
    default: return "unknown";
  }
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
