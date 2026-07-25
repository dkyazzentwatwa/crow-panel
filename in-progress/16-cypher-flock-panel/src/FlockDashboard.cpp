#include "FlockDashboard.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <math.h>

using namespace Widgets;

namespace {
constexpr int16_t kW = 1024;
constexpr int16_t kH = 600;
constexpr int16_t kHeaderH = 62;
constexpr int16_t kTabsY = 16;
constexpr int16_t kBodyY = 82;
constexpr int16_t kFooterY = 548;
constexpr int16_t kFooterH = 52;

const char *screenName(FlockScreen screen) {
  switch (screen) {
    case kFlockScopeScreen: return "SCOPE";
    case kFlockFeedScreen: return "FEED";
    case kFlockWitnessScreen: return "C6 WIFI";
    case kFlockStatsScreen: return "STATS";
    case kFlockControlScreen: return "CONTROL";
    default: return "?";
  }
}

const char *filterName(FlockFilter filter) {
  switch (filter) {
    case kFlockFilterWifi: return "WIFI";
    case kFlockFilterBle: return "BLE";
    case kFlockFilterRaven: return "RAVEN";
    case kFlockFilterCandidate: return "CAND";
    case kFlockFilterBw16: return "BW16";
    case kFlockFilterEsp32: return "ESP32";
    default: return "ALL";
  }
}

uint16_t linkColor(FlockLinkState link) {
  if (link == kFlockLinkOnline) return kGreen;
  if (link == kFlockLinkStale) return kAmber;
  if (link == kFlockLinkMock) return kAccent;
  return kRed;
}

const char *linkName(FlockLinkState link) {
  if (link == kFlockLinkOnline) return "BRIDGE LIVE";
  if (link == kFlockLinkStale) return "BRIDGE STALE";
  if (link == kFlockLinkMock) return "MOCK SOURCE";
  return "BRIDGE OFFLINE";
}

uint16_t detectionColor(const FlockDetection &d) {
  if (flockIsRaven(d)) return kRed;
  if (flockIsWifi(d)) return kAccent;
  return kAmber;
}

uint8_t signalLevel(int8_t rssi) {
  if (rssi >= -50) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -78) return 2;
  return rssi > -100 ? 1 : 0;
}

uint32_t macHash(const char *mac) {
  uint32_t hash = 2166136261u;
  for (const char *p = mac; p && *p; ++p) hash = (hash ^ (uint8_t)*p) * 16777619u;
  return hash;
}

String fit(Arduino_GFX *g, const String &value, const GFXfont *font, int16_t width) {
  if (textWidth(g, value.c_str(), font) <= width) return value;
  String out = value;
  while (out.length() > 1 && textWidth(g, (out + "...").c_str(), font) > width) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

void metric(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, const char *label,
            const String &value, uint16_t color) {
  panel(g, x, y, w, 110, 8, kSurface, 1, kLine);
  text(g, x + 16, y + 14, label, fontS(), kTextMut, kLeft);
  text(g, x + 16, y + 43, fit(g, value, fontXL(), w - 32).c_str(), fontXL(), color, kLeft);
}

void button(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, const char *label,
            const String &value, uint16_t color) {
  panel(g, x, y, w, 92, 8, kSurface, 1, color);
  text(g, x + 16, y + 13, label, fontS(), kTextMut, kLeft);
  text(g, x + 16, y + 43, fit(g, value, fontL(), w - 32).c_str(), fontL(), color, kLeft);
}
}
#endif

void FlockDashboard::begin() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "Cypher Flock Panel") &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
#endif
}

void FlockDashboard::setBanner(const String &banner) {
  banner_ = banner;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void FlockDashboard::setScreen(FlockScreen screen) {
  if ((uint8_t)screen >= (uint8_t)kFlockScreenCount) return;
  screen_ = screen;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void FlockDashboard::nextScreen() {
  setScreen((FlockScreen)(((uint8_t)screen_ + 1) % (uint8_t)kFlockScreenCount));
}

void FlockDashboard::setFilter(FlockFilter filter) {
  filter_ = filter;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void FlockDashboard::setPrevious(bool previous) {
  showingPrevious_ = previous;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void FlockDashboard::setStealth(bool enabled) {
  stealth_ = enabled;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

void FlockDashboard::requestRepaint() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
}

bool FlockDashboard::tick(const FlockDetectionStore &current,
                          const FlockDetectionStore &previous,
                          const FlockBridgeStatus &bridge,
                          const FlockLifetimeStats &lifetime,
                          const FlockC6Witness &witness,
                          bool persistenceReady, FlockUiEvent &event) {
  event.action = kFlockUiNone;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return false;
  handleTouch_(event);
  if (stealth_) {
    if (dirty_) {
      CrowDisplay::canvas()->fillScreen(0x0000);
      dirty_ = false;
    }
    return event.action != kFlockUiNone;
  }
  const FlockDetectionStore &store = activeStore_(current, previous);
  if (current.count() != lastCount_) {
    if (current.count() > lastCount_) alertUntilMs_ = millis() + 1800;
    lastCount_ = current.count();
    dirty_ = true;
  }
  if (witness.generation() != lastWitnessGeneration_ ||
      witness.scanning() != lastWitnessScanning_) {
    lastWitnessGeneration_ = witness.generation();
    lastWitnessScanning_ = witness.scanning();
    dirty_ = true;
  }
  if (dirty_) {
    drawFull_(store, bridge, lifetime, witness, persistenceReady);
    dirty_ = false;
  } else if (refreshGate_.ready()) {
    drawHeader_(bridge);
    if (screen_ == kFlockStatsScreen) drawStats_(store, bridge, lifetime, persistenceReady);
    else if (screen_ == kFlockControlScreen) drawControl_(bridge, persistenceReady);
    drawFooter_(store);
  }
  return event.action != kFlockUiNone;
#else
  (void)current;
  (void)previous;
  (void)bridge;
  (void)lifetime;
  (void)witness;
  (void)persistenceReady;
  return false;
#endif
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
const FlockDetectionStore &FlockDashboard::activeStore_(const FlockDetectionStore &current,
                                                        const FlockDetectionStore &previous) const {
  return showingPrevious_ ? previous : current;
}

void FlockDashboard::handleTouch_(FlockUiEvent &event) {
  int16_t x = 0, y = 0;
  bool touched = CrowDisplay::touchPoint(x, y);
  if (touched && !wasTouched_ && millis() - lastTouchMs_ > 120) {
    lastTouchMs_ = millis();
    if (stealth_) {
      event.action = kFlockUiStealthToggle;
    } else {
      handleTouchAt_(x, y, event);
    }
  }
  wasTouched_ = touched;
}

bool FlockDashboard::handleTouchAt_(int16_t x, int16_t y, FlockUiEvent &event) {
  if (y <= kHeaderH) {
    if (x < 330) return true;
    if (x < 418) setScreen(kFlockScopeScreen);
    else if (x < 496) setScreen(kFlockFeedScreen);
    else if (x < 606) setScreen(kFlockWitnessScreen);
    else if (x < 690) setScreen(kFlockStatsScreen);
    else if (x < 800) setScreen(kFlockControlScreen);
    return true;
  }
  if (y >= kFooterY) {
    if (screen_ == kFlockWitnessScreen) {
      if (x < 180) {
        if (witnessPage_ > 0) --witnessPage_;
        dirty_ = true;
      } else if (x < 360) {
        ++witnessPage_;
        dirty_ = true;
      } else if (x > 820) {
        event.action = kFlockUiWitnessRefresh;
      }
      return true;
    }
    if (x < 100) setFilter(kFlockFilterAll);
    else if (x < 180) setFilter(kFlockFilterWifi);
    else if (x < 250) setFilter(kFlockFilterBle);
    else if (x < 340) setFilter(kFlockFilterRaven);
    else if (x < 420) setFilter(kFlockFilterCandidate);
    else if (x < 510) setFilter(kFlockFilterBw16);
    else if (x < 610) setFilter(kFlockFilterEsp32);
    else if (x > 820) setPrevious(!showingPrevious_);
    return true;
  }
  if (screen_ == kFlockFeedScreen && y >= 120 && y <= 166) {
    sort_ = x >= 880 ? kFlockSortConfidence : (x >= 700 ? kFlockSortSignal : kFlockSortRecent);
    dirty_ = true;
    return true;
  }
  if (screen_ != kFlockControlScreen) return false;
  if (y > 410) { event.action = kFlockUiReset; return true; }
  int col = min(2, max(0, x / 341));
  int row = (y - 94) / 103;
  if (row < 0 || row > 2) return false;
  static const FlockUiAction actions[3][3] = {
      {kFlockUiPauseToggle, kFlockUiModeCycle, kFlockUiBandCycle},
      {kFlockUiChannelNext, kFlockUiProfileCycle, kFlockUiDiagnosticsToggle},
      {kFlockUiStealthToggle, kFlockUiCalibrationToggle, kFlockUiSave}};
  event.action = actions[row][col];
  return true;
}

void FlockDashboard::drawFull_(const FlockDetectionStore &store,
                               const FlockBridgeStatus &bridge,
                               const FlockLifetimeStats &lifetime,
                               const FlockC6Witness &witness,
                               bool persistenceReady) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillScreen(kBg);
  drawHeader_(bridge);
  if (screen_ == kFlockScopeScreen) drawScope_(store);
  else if (screen_ == kFlockFeedScreen) drawFeed_(store);
  else if (screen_ == kFlockWitnessScreen) drawWitness_(witness);
  else if (screen_ == kFlockStatsScreen) drawStats_(store, bridge, lifetime, persistenceReady);
  else drawControl_(bridge, persistenceReady);
  drawFooter_(store);
  if (screen_ != kFlockWitnessScreen) drawAlert_(store);
}

void FlockDashboard::drawHeader_(const FlockBridgeStatus &bridge) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(0, 0, kW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kW, 2, kAccent);
  towerIcon(g, 18, 17, kAccent);
  text(g, 58, 8, "CYPHER FLOCK", fontL(), kTextHi, kLeft);
  text(g, 58, 36, "PASSIVE WIFI / BLE FIELD DETECTOR", fontS(), kTextMut, kLeft);
  drawTabs_();
  uint16_t color = linkColor(bridge.link);
  statusDot(g, 848, 31, 6, color);
  text(g, 1004, 22, linkName(bridge.link), fontS(), color, kRight);
}

void FlockDashboard::drawTabs_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  int16_t x = 330;
  const int16_t widths[] = {82, 72, 104, 78, 104};
  for (uint8_t i = 0; i < kFlockScreenCount; ++i) {
    int16_t width = widths[i];
    bool active = screen_ == (FlockScreen)i;
    panel(g, x, kTabsY, width, 32, 6, active ? kAccent : kSurfaceHi);
    text(g, x + width / 2, kTabsY + 8, screenName((FlockScreen)i), fontS(),
         active ? kBg : kTextMut, kCenter);
    x += width + 6;
  }
}

void FlockDashboard::drawScope_(const FlockDetectionStore &store) {
  Arduino_GFX *g = CrowDisplay::canvas();
  constexpr int16_t cx = 300, cy = 310, radius = 190;
  panel(g, 18, kBodyY, 570, 450, 8, rgb(4, 12, 18), 1, kLine);
  text(g, 38, 98, "RF PROXIMITY SCOPE", fontL(), kTextHi, kLeft);
  text(g, 568, 102, "SIGNAL PROXIMITY - NOT LOCATION", fontS(), kAmber, kRight);
  for (uint8_t ring = 1; ring <= 4; ++ring) g->drawCircle(cx, cy, radius * ring / 4, ring == 4 ? kAccent : kLine);
  g->drawLine(cx - radius, cy, cx + radius, cy, kLine);
  g->drawLine(cx, cy - radius, cx, cy + radius, kLine);
  g->fillCircle(cx, cy, 5, kGreen);
  uint8_t shown = min((uint16_t)18, store.protocolCount(filter_));
  for (uint8_t i = 0; i < shown; ++i) {
    int16_t index = store.newestScopeIndex(filter_, i);
    const FlockDetection *d = index >= 0 ? store.at(index) : nullptr;
    if (!d) continue;
    float norm = constrain((float)(-d->rssi - 35) / 65.0f, 0.08f, 1.0f);
    int16_t r = 24 + (int16_t)(norm * (radius - 32));
    float angle = (float)(macHash(d->mac) % 360) * DEG_TO_RAD;
    int16_t x = cx + (int16_t)(cosf(angle) * r);
    int16_t y = cy + (int16_t)(sinf(angle) * r);
    uint16_t color = detectionColor(*d);
    g->fillCircle(x, y, d->confidence >= 85 ? 8 : 5, color);
    if (d->confidence >= 70) g->drawCircle(x, y, 12, color);
  }

  panel(g, 606, kBodyY, 400, 450, 8, kBg, 1, kLine);
  text(g, 626, 98, "LATEST CONTACTS", fontL(), kTextHi, kLeft);
  for (uint8_t i = 0; i < 7; ++i) {
    int16_t index = store.newestScopeIndex(filter_, i);
    const FlockDetection *d = index >= 0 ? store.at(index) : nullptr;
    if (!d) break;
    int16_t y = 142 + i * 52;
    statusDot(g, 630, y + 13, 5, detectionColor(*d));
    String title = d->deviceName[0] ? d->deviceName : d->mac;
    text(g, 648, y, fit(g, title, fontM(), 220).c_str(), fontM(), kTextHi, kLeft);
    char meta[96];
    snprintf(meta, sizeof(meta), "%s  %d dBm  %s", d->source[0] ? d->source :
             (flockIsWifi(*d) ? "WIFI" : (flockIsRaven(*d) ? "RAVEN" : "BLE")),
             d->rssi, d->evidence);
    text(g, 648, y + 27, meta, fontS(), kTextMut, kLeft);
    signalBars(g, 930, y + 31, signalLevel(d->rssi), detectionColor(*d));
  }
}

void FlockDashboard::drawFeed_(const FlockDetectionStore &store) {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, 18, kBodyY, 988, 450, 8, kBg, 1, kLine);
  text(g, 38, 100, "DETECTION FEED", fontL(), kTextHi, kLeft);
  text(g, 986, 104, (String(filterName(filter_)) + " / " + (showingPrevious_ ? "PREVIOUS" : "CURRENT")).c_str(), fontS(), kAccent, kRight);
  text(g, 46, 138, "TYPE", fontS(), kTextMut, kLeft);
  text(g, 154, 138, "DEVICE / MAC", fontS(), kTextMut, kLeft);
  text(g, 520, 138, "METHOD", fontS(), kTextMut, kLeft);
  text(g, 758, 138, sort_ == kFlockSortSignal ? "SIGNAL v" : "SIGNAL", fontS(), sort_ == kFlockSortSignal ? kAccent : kTextMut, kLeft);
  text(g, 930, 138, sort_ == kFlockSortConfidence ? "CONF v" : "CONF", fontS(), sort_ == kFlockSortConfidence ? kAccent : kTextMut, kRight);
  for (uint8_t i = 0; i < 8; ++i) {
    int16_t index = store.sortedIndex(filter_, sort_, i);
    const FlockDetection *d = index >= 0 ? store.at(index) : nullptr;
    if (!d) break;
    int16_t y = 170 + i * 42;
    if ((i & 1) == 0) g->fillRoundRect(32, y - 6, 960, 38, 5, kSurface);
    const char *kind = d->candidate ? "CAND" : (flockIsWifi(*d) ? (strcmp(d->band, "5") == 0 ? "WIFI5" : "WIFI") : (flockIsRaven(*d) ? "RAVEN" : "BLE"));
    text(g, 46, y, kind, fontS(), detectionColor(*d), kLeft);
    String device = d->deviceName[0] ? String(d->deviceName) + " / " + d->mac : d->mac;
    text(g, 154, y - 2, fit(g, device, fontM(), 340).c_str(), fontM(), kTextHi, kLeft);
    String method = String(d->evidence) + " / " + d->method;
    text(g, 520, y, fit(g, method, fontS(), 220).c_str(), fontS(), kTextMut, kLeft);
    char signal[24];
    if (d->directRssi) snprintf(signal, sizeof(signal), "%d dBm ch%u", d->rssi, d->channel);
    else snprintf(signal, sizeof(signal), "inferred ch%u", d->channel);
    text(g, 758, y, signal, fontS(), kTextHi, kLeft);
    text(g, 930, y, String(d->confidence).c_str(), fontL(), detectionColor(*d), kRight);
  }
}

void FlockDashboard::drawWitness_(const FlockC6Witness &witness) {
  Arduino_GFX *g = CrowDisplay::canvas();
  constexpr uint8_t rowsPerPage = 8;
  uint8_t pages = max((uint8_t)1, (uint8_t)((witness.count() + rowsPerPage - 1) / rowsPerPage));
  if (witnessPage_ >= pages) witnessPage_ = pages - 1;
  uint16_t first = (uint16_t)witnessPage_ * rowsPerPage;

  panel(g, 18, kBodyY, 988, 450, 8, kBg, 1, kLine);
  text(g, 38, 96, "C6 PASSIVE WIFI WITNESS", fontL(), kTextHi, kLeft);
  text(g, 986, 100, witness.hardwareEnabled() ? "HOSTED C6 / 2.4 GHZ" : "DETERMINISTIC MOCK",
       fontS(), witness.hardwareEnabled() ? kGreen : kAccent, kRight);
  text(g, 38, 124, "AP metadata only - no raw frames - no Flock confidence impact",
       fontS(), kAmber, kLeft);

  char summary[128];
  snprintf(summary, sizeof(summary), "%u stored / %u found   scans %lu   %s",
           witness.count(), witness.totalFound(), (unsigned long)witness.scanCount(),
           witness.scanning() ? "SCANNING" : witness.status());
  text(g, 986, 124, fit(g, summary, fontS(), 500).c_str(), fontS(), kTextMut, kRight);

  g->fillRoundRect(30, 151, 964, 30, 5, kSurfaceHi);
  text(g, 42, 159, "RSSI", fontS(), kTextMut, kLeft);
  text(g, 102, 159, "CH", fontS(), kTextMut, kLeft);
  text(g, 145, 159, "SSID", fontS(), kTextMut, kLeft);
  text(g, 375, 159, "BSSID", fontS(), kTextMut, kLeft);
  text(g, 525, 159, "SECURITY / CIPHER", fontS(), kTextMut, kLeft);
  text(g, 710, 159, "PHY", fontS(), kTextMut, kLeft);
  text(g, 790, 159, "BW", fontS(), kTextMut, kLeft);
  text(g, 840, 159, "FLAGS", fontS(), kTextMut, kLeft);
  text(g, 974, 159, "CC", fontS(), kTextMut, kRight);

  for (uint8_t row = 0; row < rowsPerPage; ++row) {
    const FlockWitnessNetwork *network = witness.at(first + row);
    if (!network) break;
    int16_t y = 190 + row * 39;
    if ((row & 1) == 0) g->fillRoundRect(30, y - 6, 964, 34, 4, kSurface);
    uint16_t signalColor = network->rssi >= -55 ? kGreen :
                           (network->rssi >= -75 ? kAmber : kTextMut);
    text(g, 42, y, String(network->rssi).c_str(), fontM(), signalColor, kLeft);
    text(g, 102, y + 2, String(network->channel).c_str(), fontS(), kTextHi, kLeft);
    text(g, 145, y, fit(g, network->ssid, fontM(), 215).c_str(), fontM(),
         network->hidden ? kAmber : kTextHi, kLeft);
    text(g, 375, y + 2, network->bssid, fontS(), kTextMut, kLeft);
    String security = String(network->auth) + " " + network->pairwiseCipher + "/" +
                      network->groupCipher;
    text(g, 525, y + 2, fit(g, security, fontS(), 174).c_str(), fontS(), kTextMut, kLeft);
    text(g, 710, y + 2, fit(g, network->phy, fontS(), 74).c_str(), fontS(), kAccent, kLeft);
    String bandwidth = String(network->bandwidthMhz) + "M";
    if (network->secondaryChannel == 1) bandwidth += "+";
    else if (network->secondaryChannel == 2) bandwidth += "-";
    text(g, 790, y + 2, bandwidth.c_str(), fontS(), kTextHi, kLeft);
    String flags;
    if (network->wps) flags += "WPS ";
    if (network->ftmResponder) flags += "FR ";
    if (network->ftmInitiator) flags += "FI ";
    if (network->bssColor) flags += "B" + String(network->bssColor);
    if (!flags.length()) flags = "-";
    text(g, 840, y + 2, fit(g, flags, fontS(), 104).c_str(), fontS(), kTextMut, kLeft);
    text(g, 974, y + 2, network->country, fontS(), kTextMut, kRight);
  }

  char page[64];
  snprintf(page, sizeof(page), "PAGE %u/%u   age %lus", witnessPage_ + 1, pages,
           (unsigned long)(witness.scanAgeMs() / 1000));
  text(g, 986, 512, page, fontS(), kAccent, kRight);
}

void FlockDashboard::drawStats_(const FlockDetectionStore &store,
                                const FlockBridgeStatus &bridge,
                                const FlockLifetimeStats &lifetime,
                                bool persistenceReady) {
  Arduino_GFX *g = CrowDisplay::canvas();
  metric(g, 18, 92, 230, "SESSION DEVICES", String(store.count()), kTextHi);
  metric(g, 266, 92, 230, "WIFI", String(store.protocolCount(kFlockFilterWifi)), kAccent);
  metric(g, 514, 92, 230, "BLE", String(store.protocolCount(kFlockFilterBle)), kAmber);
  metric(g, 762, 92, 244, "RAVEN", String(store.protocolCount(kFlockFilterRaven)), kRed);

  panel(g, 18, 220, 620, 310, 8, kSurface, 1, kLine);
  text(g, 38, 238, "THREE-NODE DIAGNOSTICS", fontL(), kTextHi, kLeft);
  char row[128];
  snprintf(row, sizeof(row), "BLE %u   BW16 %u   AGG %u", bridge.bleLink, bridge.bw16Link, bridge.aggregatorLink);
  text(g, 42, 282, row, fontM(), kTextHi, kLeft);
  snprintf(row, sizeof(row), "%s band=%s mode=%s ch=%u%s", bridge.radioPhase, bridge.band, bridge.mode,
           bridge.channel, bridge.fallback ? " FALLBACK" : "");
  text(g, 42, 328, row, fontM(), kTextMut, kLeft);
  snprintf(row, sizeof(row), "frames %lu/%lu  BLE %lu  drops %lu", (unsigned long)bridge.wifiMgmtFrames,
           (unsigned long)bridge.wifiDataFrames, (unsigned long)bridge.bleReports,
           (unsigned long)bridge.queueDrops);
  text(g, 42, 374, row, fontM(), kTextMut, kLeft);
  snprintf(row, sizeof(row), "listen 2.4/5 %lu/%lu s  sweeps %lu err %lu", (unsigned long)(bridge.listen24Ms / 1000),
           (unsigned long)(bridge.listen5Ms / 1000), (unsigned long)bridge.fullSweeps,
           (unsigned long)(bridge.parserErrors + bridge.channelErrors));
  text(g, 42, 420, row, fontM(), kTextMut, kLeft);
  text(g, 42, 468, "30s ACTIVITY", fontS(), kTextMut, kLeft);
  const uint16_t *activity = store.activity();
  for (uint8_t index = 0; index < 30; ++index) {
    uint8_t slot = (uint8_t)((store.activityHead() + index + 1) % 30);
    int16_t height = min(32, (int)activity[slot] * 4);
    g->fillRect(164 + index * 14, 512 - height, 9, height, kAccent);
  }
  text(g, 42, 506, persistenceReady ? "FFAT V2 READY" : "RAM ONLY", fontS(), persistenceReady ? kGreen : kAmber, kLeft);

  panel(g, 656, 220, 350, 310, 8, kSurface, 1, kLine);
  text(g, 676, 238, "LIFETIME COUNTERS", fontL(), kTextHi, kLeft);
  text(g, 682, 300, "WIFI", fontS(), kTextMut, kLeft);
  text(g, 980, 286, String(lifetime.wifi).c_str(), fontXL(), kAccent, kRight);
  text(g, 682, 374, "BLE", fontS(), kTextMut, kLeft);
  text(g, 980, 360, String(lifetime.ble).c_str(), fontXL(), kAmber, kRight);
  text(g, 682, 448, "RAVEN", fontS(), kTextMut, kLeft);
  text(g, 980, 434, String(lifetime.raven).c_str(), fontXL(), kRed, kRight);
}

void FlockDashboard::drawControl_(const FlockBridgeStatus &bridge, bool persistenceReady) {
  Arduino_GFX *g = CrowDisplay::canvas();
  const int16_t xs[] = {18, 356, 694};
  const int16_t ys[] = {92, 195, 298};
  const char *labels[3][3] = {{"SCANNER", "HOP MODE", "BAND"}, {"CHANNEL", "PROFILE", "DIAGNOSTICS"}, {"STEALTH", "CALIBRATION", "SESSION SAVE"}};
  String values[3][3] = {{bridge.scanState, bridge.mode, bridge.band}, {String(bridge.channel), bridge.profile,
      bridge.diagnostics ? "ON" : "OFF"}, {stealth_ ? "ON" : "OFF", "TOGGLE", persistenceReady ? "FFAT" : "RAM"}};
  for (uint8_t row = 0; row < 3; ++row) for (uint8_t col = 0; col < 3; ++col)
    button(g, xs[col], ys[row], 312, labels[row][col], values[row][col], col == 0 && row == 0 ? kGreen : kAccent);
  panel(g, 24, 410, 976, 112, 8, kSurface, 1, kRed);
  text(g, 44, 438, "RESET CURRENT SESSION", fontL(), kRed, kLeft);
  text(g, 980, 442, "TAP TWICE TO CONFIRM", fontS(), kTextMut, kRight);
}

void FlockDashboard::drawFooter_(const FlockDetectionStore &store) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRect(0, kFooterY, kW, kFooterH, kSurface);
  if (screen_ == kFlockWitnessScreen) {
    pill(g, 18, kFooterY + 12, "< PREV", fontS(), kTextHi, kSurfaceHi);
    pill(g, 126, kFooterY + 12, "NEXT >", fontS(), kTextHi, kSurfaceHi);
    text(g, 512, kFooterY + 18, "C6 WITNESS IS OBSERVATION-ONLY", fontS(), kAmber, kCenter);
    pill(g, 900, kFooterY + 12, "RESCAN", fontS(), kBg, kAccent);
    return;
  }
  int16_t x = 18;
  for (uint8_t i = 0; i < 7; ++i) {
    FlockFilter f = (FlockFilter)i;
    bool active = filter_ == f;
    x += pill(g, x, kFooterY + 12, filterName(f), fontS(), active ? kBg : kTextMut,
              active ? kAccent : kSurfaceHi) + 8;
  }
  String summary = String(store.count()) + " devices  |  " + banner_;
  text(g, 600, kFooterY + 18, fit(g, summary, fontS(), 260).c_str(), fontS(), kTextMut, kLeft);
  text(g, 1006, kFooterY + 18, showingPrevious_ ? "VIEW: PREVIOUS" : "VIEW: CURRENT", fontS(), showingPrevious_ ? kAmber : kGreen, kRight);
}

void FlockDashboard::drawAlert_(const FlockDetectionStore &store) {
  if (millis() >= alertUntilMs_ || store.count() == 0) return;
  int16_t index = store.newestIndex(kFlockFilterAll, 0);
  const FlockDetection *d = index >= 0 ? store.at(index) : nullptr;
  if (!d || !d->alertEligible) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, 300, 236, 424, 116, 10, kSurfaceHi, 2, detectionColor(*d));
  text(g, 512, 252, d->rediscovered ? "TARGET REDISCOVERED" : "NEW TARGET", fontL(), detectionColor(*d), kCenter);
  text(g, 512, 294, d->deviceName[0] ? d->deviceName : d->mac, fontM(), kTextHi, kCenter);
  char meta[64];
  snprintf(meta, sizeof(meta), "%d dBm  confidence %u%%", d->rssi, d->confidence);
  text(g, 512, 326, meta, fontS(), kTextMut, kCenter);
}
#endif
