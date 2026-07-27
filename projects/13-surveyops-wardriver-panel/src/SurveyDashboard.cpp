#include "SurveyDashboard.h"

#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <math.h>
#include <stdio.h>

using namespace Widgets;

namespace {
constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 64;
constexpr int16_t kScopeX = 32;
constexpr int16_t kScopeY = 94;
constexpr int16_t kScopeW = 432;
constexpr int16_t kScopeH = 340;
constexpr int16_t kScopeCx = kScopeX + kScopeW / 2;
constexpr int16_t kScopeCy = kScopeY + kScopeH / 2 + 8;
constexpr int16_t kScopeR = 142;
constexpr int16_t kPanelX = 496;
constexpr int16_t kPanelW = 496;
constexpr int16_t kListY = 94;
constexpr int16_t kScanButtonX = kPanelX + kPanelW - 154;
constexpr int16_t kScanButtonY = kListY + 8;
constexpr int16_t kScanButtonW = 136;
constexpr int16_t kScanButtonH = 34;
constexpr int16_t kRowsTop = 136;
constexpr int16_t kRowH = 50;
constexpr uint8_t kVisibleRows = 4;
constexpr int16_t kPageButtonY = 342;
constexpr int16_t kPageButtonW = 112;
constexpr int16_t kPageButtonH = 32;
constexpr int16_t kPrevButtonX = kPanelX + 18;
constexpr int16_t kNextButtonX = kPanelX + kPanelW - 18 - kPageButtonW;
constexpr int16_t kGpsY = 446;
constexpr int16_t kCardH = 88;
constexpr int16_t kDetailY = 404;
constexpr int16_t kDetailH = 130;
constexpr int16_t kFooterY = 548;
constexpr int16_t kFooterH = 52;
constexpr uint16_t kTouchDebounceMs = 120;
constexpr float kSweepDegPerMs = 360.0f / 5200.0f;

String fitText(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String t = s;
  while (t.length() > 1) {
    String probe = t + "...";
    if (textWidth(g, probe.c_str(), font) <= maxW) return probe;
    t.remove(t.length() - 1);
  }
  return ".";
}

float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

uint8_t signalLevel(int32_t rssi) {
  if (rssi >= -50) return 4;
  if (rssi >= -62) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -88) return 1;
  return 0;
}

uint16_t signalColor(int32_t rssi) {
  if (rssi >= -62) return kGreen;
  if (rssi >= -78) return kAmber;
  return kRed;
}

uint16_t authColor(const String &authMode) {
  String a = authMode;
  a.toUpperCase();
  if (a.indexOf("OPEN") >= 0 || a.indexOf("FEED") >= 0) return kAmber;
  if (a.indexOf("WPA3") >= 0) return kGreen;
  return kAccent;
}

String flagLabel(const char *onText, const char *offText, bool on) {
  return on ? String(onText) : String(offText);
}

String shortBssid(const String &bssid) {
  if (bssid.length() <= 8) return bssid;
  return bssid.substring(bssid.length() - 8);
}

const GFXfont *uiTitleFont() {
  return fontS();
}

const GFXfont *uiValueFont() {
  return fontS();
}
}  // namespace

void SurveyDashboard::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "SurveyOps Wardriver Panel", true) &&
           (CrowDisplay::canvas() != nullptr);
  if (!ready_) return;
  lastFrameMs_ = millis();
  drawFull_();
  CrowDisplay::flush();
  dirty_ = false;
}

void SurveyDashboard::update(const SurveyDashboardState &state) {
  if (!ready_) return;
  state_ = state;
  if (selectedRow_ >= (int8_t)state_.rowCount) {
    selectedRow_ = state_.rowCount > 0 ? 0 : -1;
  }
  if (pageStart_ >= state_.rowCount && state_.rowCount > 0) {
    pageStart_ = (uint8_t)(((state_.rowCount - 1) / kVisibleRows) * kVisibleRows);
  }
  if (state_.rowCount == 0) {
    pageStart_ = 0;
  }
  if (selectedRow_ < 0 && state_.rowCount > 0) {
    selectedRow_ = 0;
  }
  dirty_ = true;
}

bool SurveyDashboard::tick() {
  if (!ready_) return false;
  handleTouch_();

  uint32_t now = millis();
  float dt = (float)(now - lastFrameMs_);
  lastFrameMs_ = now;
  sweepDeg_ += dt * kSweepDegPerMs;
  while (sweepDeg_ >= 360.0f) sweepDeg_ -= 360.0f;

  if (dirty_) {
    drawFull_();
    CrowDisplay::flush();
    dirty_ = false;
    return takeScanRequest();
  }
  bool drew = false;
  if (frameGate_.ready()) {
    drawScope_();
    drew = true;
  }
  if (footerGate_.ready()) {
    drawHeader_();
    drawFooter_();
    drew = true;
  }
  if (drew) {
    CrowDisplay::flush();
  }
  return takeScanRequest();
}

bool SurveyDashboard::takeScanRequest() {
  bool requested = scanRequested_;
  scanRequested_ = false;
  return requested;
}

void SurveyDashboard::handleTouch_() {
  int16_t rx, ry;
  bool touched = CrowDisplay::touchPoint(rx, ry);
  uint32_t now = millis();
  bool shouldProcess = touched && !wasTouched_ && (now - lastTouchActionMs_) > kTouchDebounceMs;
  if (shouldProcess) {
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
    if (!handled) {
      Logger::info("touch", "ignored raw=" + String(rx) + "," + String(ry));
    }
    lastTouchActionMs_ = now;
  }
  wasTouched_ = touched;
}

bool SurveyDashboard::handleTouchAt_(int16_t tx, int16_t ty, const char *mapping) {
  if (tx >= kScanButtonX && tx <= kScanButtonX + kScanButtonW &&
      ty >= kScanButtonY && ty <= kScanButtonY + kScanButtonH) {
    requestScan_(mapping);
    return true;
  }

  if (tx >= kPrevButtonX && tx <= kPrevButtonX + kPageButtonW &&
      ty >= kPageButtonY && ty <= kPageButtonY + kPageButtonH) {
    if (pageStart_ >= kVisibleRows) {
      pageStart_ -= kVisibleRows;
      selectedRow_ = pageStart_;
      dirty_ = true;
    }
    Logger::info("touch", String("map=") + mapping + " action=page-prev");
    return true;
  }
  if (tx >= kNextButtonX && tx <= kNextButtonX + kPageButtonW &&
      ty >= kPageButtonY && ty <= kPageButtonY + kPageButtonH) {
    if (pageStart_ + kVisibleRows < state_.rowCount) {
      pageStart_ += kVisibleRows;
      selectedRow_ = pageStart_;
      dirty_ = true;
    }
    Logger::info("touch", String("map=") + mapping + " action=page-next");
    return true;
  }

  int8_t hit = hitTestRow_(tx, ty);
  if (hit >= 0) {
    selectedRow_ = hit;
    dirty_ = true;
    Logger::info("touch", String("map=") + mapping + " action=ap-row idx=" + String(hit));
    return true;
  }

  long dx = tx - kScopeCx;
  long dy = ty - kScopeCy;
  if (dx * dx + dy * dy <= (long)(kScopeR + 36) * (kScopeR + 36)) {
    cycleSelected_();
    dirty_ = true;
    Logger::info("touch", String("map=") + mapping + " action=cycle-ap");
    return true;
  }
  return false;
}

void SurveyDashboard::requestScan_(const char *mapping) {
  scanRequested_ = true;
  dirty_ = true;
  Logger::info("touch", String("map=") + mapping + " action=scan");
}

int8_t SurveyDashboard::hitTestRow_(int16_t tx, int16_t ty) const {
  if (tx < kPanelX || tx > kPanelX + kPanelW || ty < kRowsTop - 8) return -1;
  uint8_t rows = state_.rowCount < kVisibleRows ? state_.rowCount : kVisibleRows;
  for (uint8_t i = 0; i < rows; i++) {
    int16_t y = kRowsTop + i * kRowH;
    if (ty >= y - 8 && ty <= y + kRowH - 2) return (int8_t)(pageStart_ + i);
  }
  return -1;
}

void SurveyDashboard::cycleSelected_() {
  if (state_.rowCount == 0) {
    selectedRow_ = -1;
    return;
  }
  uint8_t pageEnd = pageStart_ + kVisibleRows;
  if (pageEnd > state_.rowCount) pageEnd = state_.rowCount;
  selectedRow_++;
  if (selectedRow_ < (int8_t)pageStart_ || selectedRow_ >= (int8_t)pageEnd) {
    selectedRow_ = pageStart_;
  }
}

void SurveyDashboard::drawFull_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  drawHeader_();
  drawScope_();
  drawApList_();
  drawGpsCard_();
  drawDetailCard_();
  drawFooter_();
}

void SurveyDashboard::drawHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);

  towerIcon(g, 18, 18, kAccent);
  text(g, 58, 10, "SURVEYOPS", uiTitleFont(), kTextHi, kLeft);
  text(g, 58, 38, "PASSIVE GPS / WIFI / WIGLE PANEL", fontS(), kTextMut, kLeft);

  char up[18];
  snprintf(up, sizeof(up), "UP %lus", (unsigned long)(millis() / 1000));
  String gps = flagLabel("GPS UART", "GPS MOCK", USE_GPS_DRIVER);
  String wifi = flagLabel("SCAN HW", "WIFI MOCK", USE_WIFI_SCAN);
  String sd = state_.storage.flagEnabled ? "SD CSV" : "MOCK CSV";
  String logging = state_.storage.loggingEnabled ? "LOG ON" : "LOG OFF";

  int16_t gap = 10;
  int16_t x = kScreenW - 16;
  auto drawPillRight = [&](const String &label, uint16_t fg, uint16_t fill) {
    int16_t w = textWidth(g, label.c_str(), fontS()) + 24;
    x -= w;
    pill(g, x, 18, label.c_str(), fontS(), fg, fill);
    x -= gap;
  };
  drawPillRight(up, kTextHi, kSurfaceHi);
  drawPillRight(logging, state_.storage.loggingEnabled ? kBg : kTextMut,
                state_.storage.loggingEnabled ? kGreen : kSurfaceHi);
  drawPillRight(sd, state_.storage.ready ? kBg : kTextMut,
                state_.storage.ready ? kAccent : kSurfaceHi);
  drawPillRight(wifi, USE_WIFI_SCAN ? kBg : kTextMut, USE_WIFI_SCAN ? kAccent : kSurfaceHi);
  drawPillRight(gps, state_.fix.valid ? kBg : kTextMut,
                state_.fix.valid ? kGreen : kSurfaceHi);
}

void SurveyDashboard::drawScope_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  panel(g, kScopeX, kScopeY, kScopeW, kScopeH, 8, rgb(5, 12, 18), 1, kLine);
  text(g, kScopeX + 18, kScopeY + 14, "SURVEY FIELD", uiTitleFont(), kTextHi, kLeft);
  text(g, kScopeX + kScopeW - 18, kScopeY + 18, state_.fix.valid ? "GPS FIX" : "WAITING",
       fontS(), state_.fix.valid ? kGreen : kAmber, kRight);

  for (uint8_t i = 1; i <= 4; i++) {
    int16_t r = kScopeR * i / 4;
    g->drawCircle(kScopeCx, kScopeCy, r, i == 4 ? kAccent : kLine);
  }
  for (int a = 0; a < 360; a += 30) {
    float rad = (a - 90) * DEG_TO_RAD;
    g->drawLine(kScopeCx, kScopeCy, kScopeCx + (int16_t)(cosf(rad) * kScopeR),
                kScopeCy + (int16_t)(sinf(rad) * kScopeR), kLine);
  }

  for (float trail = 48.0f; trail >= 0.0f; trail -= 4.0f) {
    float a = sweepDeg_ - trail;
    float intensity = (48.0f - trail) / 48.0f;
    uint16_t col = rgb(0, (uint8_t)(32 + intensity * 140), (uint8_t)(40 + intensity * 120));
    float rad = (a - 90.0f) * DEG_TO_RAD;
    g->drawLine(kScopeCx, kScopeCy, kScopeCx + (int16_t)(cosf(rad) * kScopeR),
                kScopeCy + (int16_t)(sinf(rad) * kScopeR), col);
  }

  g->fillCircle(kScopeCx, kScopeCy, 5, state_.fix.valid ? kGreen : kAmber);
  g->drawCircle(kScopeCx, kScopeCy, 10, kSurfaceHi);
  text(g, kScopeCx, kScopeCy + kScopeR + 14, fitText(g, state_.fix.coordinateText(), fontS(), 260).c_str(),
       fontS(), kTextMut, kCenter);

  uint8_t rows = state_.rowCount < kSurveyMaxRows ? state_.rowCount : kSurveyMaxRows;
  for (uint8_t i = 0; i < rows; i++) {
    const WifiApRecord &row = state_.rows[i];
    float distanceNorm = clamp01((float)(-row.rssi - 35) / 55.0f);
    int16_t r = 34 + (int16_t)(distanceNorm * (kScopeR - 44));
    int angle = ((int)row.channel * 31 + i * 57 + 18) % 360;
    float rad = (angle - 90) * DEG_TO_RAD;
    int16_t x = kScopeCx + (int16_t)(cosf(rad) * r);
    int16_t y = kScopeCy + (int16_t)(sinf(rad) * r);
    bool selected = selectedRow_ == (int8_t)i;
    uint16_t col = signalColor(row.rssi);

    if (selected) {
      g->drawCircle(x, y, 15, kAccent);
      g->drawCircle(x, y, 16, kAccent);
    }
    g->fillCircle(x, y, selected ? 7 : 5, col);
    g->drawCircle(x, y, 10, authColor(row.authMode));
    if (selected || row.rssi >= -65) {
      text(g, x + 12, y - 12, fitText(g, row.ssid, fontS(), 120).c_str(), fontS(), kTextHi, kLeft);
    }
  }

  char count[32];
  snprintf(count, sizeof(count), "%u AP rows", (unsigned)state_.rowCount);
  text(g, kScopeX + 18, kScopeY + kScopeH - 34, count, fontS(), kTextHi, kLeft);
  text(g, kScopeX + kScopeW - 18, kScopeY + kScopeH - 34,
       fitText(g, "top: " + state_.topAp, fontS(), 210).c_str(), fontS(), kTextMut, kRight);
}

void SurveyDashboard::drawApList_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  panel(g, kPanelX, kListY, kPanelW, 294, 8, kBg, 1, kLine);
  text(g, kPanelX + 18, kListY + 18, "PASSIVE AP ROWS", uiTitleFont(), kTextHi, kLeft);
  char summary[32];
  uint8_t page = pageStart_ / kVisibleRows + 1;
  uint8_t pages = state_.rowCount == 0 ? 1 : (state_.rowCount + kVisibleRows - 1) / kVisibleRows;
  snprintf(summary, sizeof(summary), "%u total  page %u/%u", (unsigned)state_.totalAps,
           (unsigned)page, (unsigned)pages);
  touchButton(g, kScanButtonX, kScanButtonY, kScanButtonW, kScanButtonH, "SCAN NOW", false,
              kAccent);
  text(g, kPanelX + 18, kListY + 44, summary, fontS(), kTextMut, kLeft);

  uint8_t rows = state_.rowCount > pageStart_ ? state_.rowCount - pageStart_ : 0;
  if (rows > kVisibleRows) rows = kVisibleRows;
  for (uint8_t i = 0; i < rows; i++) {
    uint8_t rowIndex = pageStart_ + i;
    const WifiApRecord &row = state_.rows[rowIndex];
    int16_t y = kRowsTop + i * kRowH;
    bool selected = selectedRow_ == (int8_t)rowIndex;
    if (selected) {
      g->fillRoundRect(kPanelX + 10, y - 5, kPanelW - 20, kRowH - 4, 6, kSurfaceHi);
      g->drawRoundRect(kPanelX + 10, y - 5, kPanelW - 20, kRowH - 4, 6, kAccent);
    }

    statusDot(g, kPanelX + 30, y + 18, 5, authColor(row.authMode));
    text(g, kPanelX + 48, y, fitText(g, row.ssid, uiValueFont(), 240).c_str(), uiValueFont(), kTextHi, kLeft);

    char meta[64];
    snprintf(meta, sizeof(meta), "ch%u  %ld dBm  %s", (unsigned)row.channel, (long)row.rssi,
             row.authMode.c_str());
    text(g, kPanelX + 48, y + 24, meta, fontS(), kTextMut, kLeft);
    signalBars(g, kPanelX + kPanelW - 88, y + 34, signalLevel(row.rssi), signalColor(row.rssi));
    text(g, kPanelX + kPanelW - 18, y + 8, shortBssid(row.bssid).c_str(), fontS(), kTextMut,
         kRight);
    g->drawFastHLine(kPanelX + 18, y + kRowH - 6, kPanelW - 36, kLine);
  }

  if (state_.rowCount == 0) {
    text(g, kPanelX + kPanelW / 2, kRowsTop + 48, "RUN scan OR feed ap", uiTitleFont(), kTextMut,
         kCenter);
    text(g, kPanelX + kPanelW / 2, kRowsTop + 88, "No active tests. No joins. No credential capture.",
         fontS(), kTextMut, kCenter);
  }
  touchButton(g, kPrevButtonX, kPageButtonY, kPageButtonW, kPageButtonH, "PREV", false);
  touchButton(g, kNextButtonX, kPageButtonY, kPageButtonW, kPageButtonH, "NEXT", false,
              kAccent);
}

void SurveyDashboard::drawGpsCard_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  panel(g, kScopeX, kGpsY, kScopeW, kCardH, 8, kSurface, 1, kLine);
  statusDot(g, kScopeX + 26, kGpsY + 34, 6, state_.fix.valid ? kGreen : kAmber);
  text(g, kScopeX + 46, kGpsY + 16, "GPS FIX", fontS(), kAccent, kLeft);
  text(g, kScopeX + 46, kGpsY + 42, fitText(g, state_.fix.coordinateText(), uiValueFont(), 240).c_str(),
       uiValueFont(), kTextHi, kLeft);
  text(g, kScopeX + kScopeW - 18, kGpsY + 16, state_.fix.source.c_str(), fontS(), kTextMut,
       kRight);
  text(g, kScopeX + kScopeW - 18, kGpsY + 46, fitText(g, state_.fix.qualityText(), fontS(), 170).c_str(),
       fontS(), kTextMut, kRight);
}

void SurveyDashboard::drawDetailCard_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  panel(g, kPanelX, kDetailY, kPanelW, kDetailH, 8, kSurface, 1, kLine);

  if (selectedRow_ >= 0 && selectedRow_ < (int8_t)state_.rowCount) {
    const WifiApRecord &row = state_.rows[selectedRow_];
    text(g, kPanelX + 18, kDetailY + 16, "SELECTED AP", fontS(), kAccent, kLeft);
    text(g, kPanelX + 18, kDetailY + 44, fitText(g, row.ssid, uiValueFont(), 260).c_str(), uiValueFont(),
         kTextHi, kLeft);

    char l1[80];
    snprintf(l1, sizeof(l1), "RSSI %ld dBm   channel %u   auth %s", (long)row.rssi,
             (unsigned)row.channel, row.authMode.c_str());
    text(g, kPanelX + 18, kDetailY + 78, l1, fontS(), kTextMut, kLeft);
    text(g, kPanelX + kPanelW - 18, kDetailY + 44, shortBssid(row.bssid).c_str(), fontS(),
         kTextMut, kRight);
  } else {
    text(g, kPanelX + 18, kDetailY + 16, state_.detailTitle.c_str(), fontS(), kAccent, kLeft);
    String body = state_.detailBody;
    int16_t y = kDetailY + 48;
    while (body.length() > 0 && y < kDetailY + kDetailH - 18) {
      int cut = body.indexOf('|');
      String line = cut >= 0 ? body.substring(0, cut) : body;
      body = cut >= 0 ? body.substring(cut + 1) : "";
      line.trim();
      text(g, kPanelX + 18, y, fitText(g, line, fontS(), kPanelW - 36).c_str(), fontS(), kTextHi,
           kLeft);
      y += 24;
    }
  }

  WigleStorageHealth h = state_.storage;
  String logLine = String(h.loggingEnabled ? "logging on" : "logging off") + " / " + h.activeFile;
  text(g, kPanelX + 18, kDetailY + kDetailH - 28, fitText(g, logLine, fontS(), 290).c_str(), fontS(),
       h.loggingEnabled ? kGreen : kTextMut, kLeft);
  char rows[32];
  snprintf(rows, sizeof(rows), "%lu rows / %u rotations", (unsigned long)h.rowsWritten,
           (unsigned)h.rotations);
  text(g, kPanelX + kPanelW - 18, kDetailY + kDetailH - 28, rows, fontS(), kTextMut, kRight);
}

void SurveyDashboard::drawFooter_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, kLine);
  statusDot(g, 20, kFooterY + 26, 5, kAccent);
  text(g, 38, kFooterY + 18, fitText(g, state_.banner, fontS(), 520).c_str(), fontS(), kTextHi,
       kLeft);
  char stats[96];
  snprintf(stats, sizeof(stats), "session  %u scans  %lu APs  %lu logged  %u rotations",
           (unsigned)state_.session.scans, (unsigned long)state_.session.apRows,
           (unsigned long)state_.storage.rowsWritten, (unsigned)state_.storage.rotations);
  text(g, kScreenW - 18, kFooterY + 18, stats, fontS(), kTextMut, kRight);
}

#else

void SurveyDashboard::begin() {}
void SurveyDashboard::update(const SurveyDashboardState &) {}
bool SurveyDashboard::tick() { return false; }
bool SurveyDashboard::takeScanRequest() { return false; }

#endif
