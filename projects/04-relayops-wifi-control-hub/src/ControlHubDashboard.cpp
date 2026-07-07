#include "ControlHubDashboard.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <math.h>

namespace {
using namespace Widgets;

// --- Layout (1024 x 600 landscape), shared with the FieldOps dashboard. ---
constexpr int16_t kScreenW = 1024;
constexpr int16_t kHeaderH = 72;

constexpr int16_t kCardX = 16;
constexpr int16_t kCardW = 336;
constexpr int16_t kCardH = 72;
constexpr int16_t kCardGap = 8;
constexpr int16_t kCardTop = 112;

constexpr int16_t kRX = 372;
constexpr int16_t kRW = kScreenW - kRX - 16;  // 636
constexpr int16_t kBannerY = 88;
constexpr int16_t kBannerH = 44;

constexpr int16_t kGaugeCy = 232;
constexpr int16_t kGaugeROut = 86;
constexpr int16_t kGaugeRIn = 68;

constexpr int16_t kStatsY = 344;
constexpr int16_t kSparkY = 404;
constexpr int16_t kSparkH = 104;

constexpr int16_t kFooterY = 540;
constexpr int16_t kFooterH = 60;

// The hub is a Wi-Fi server, not a radio; the link pill reflects that.
#if USE_WIFI
const char *kLinkLabel = "WIFI";
constexpr bool kLinkAccent = true;
#else
const char *kLinkLabel = "WIFI MOCK";
constexpr bool kLinkAccent = false;
#endif

int16_t cardYPos(uint8_t slot) { return kCardTop + slot * (kCardH + kCardGap); }

uint8_t rssiLevel(int rssi) {
  if (rssi >= -60) return 4;
  if (rssi >= -75) return 3;
  if (rssi >= -90) return 2;
  if (rssi >= -105) return 1;
  return 0;
}

uint16_t batteryColor(float pct) {
  if (pct < 35.0f) return kRed;
  if (pct < 55.0f) return kAmber;
  return kGreen;
}

uint16_t tempColor(float c) {
  if (c > 35.0f) return kRed;
  if (c > 27.0f) return kAmber;
  return kAccent;
}

// Trim `s` (copy) until it fits `maxW` px in `font`, adding an ellipsis.
String fit(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String t = s;
  while (t.length() > 1 && textWidth(g, (t + "...").c_str(), font) > maxW) {
    t.remove(t.length() - 1);
  }
  return t + "...";
}
}  // namespace

int8_t ControlHubDashboard::findOrAddEntry(const String &name, EntryKind kind) {
  for (uint8_t i = 0; i < kMaxEntries; i++) {
    if (entries_[i].used && entries_[i].kind == kind && entries_[i].name == name) {
      return (int8_t)i;
    }
  }
  for (uint8_t i = 0; i < kMaxEntries; i++) {
    if (!entries_[i].used) {
      entries_[i] = Entry();
      entries_[i].name = name;
      entries_[i].kind = kind;
      entries_[i].used = true;
      entryCount_++;
      return (int8_t)i;
    }
  }
  // Full: evict the stalest entry.
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < kMaxEntries; i++) {
    if (entries_[i].lastSeenMs < entries_[oldest].lastSeenMs) oldest = i;
  }
  entries_[oldest] = Entry();
  entries_[oldest].name = name;
  entries_[oldest].kind = kind;
  entries_[oldest].used = true;
  return (int8_t)oldest;
}

int8_t ControlHubDashboard::activeEntry() const {
  if (pinned_ >= 0 && entries_[pinned_].used) return pinned_;
  if (lastSensor_ >= 0 && entries_[lastSensor_].used) return lastSensor_;
  for (uint8_t i = 0; i < kMaxEntries; i++) {
    if (entries_[i].used && entries_[i].kind == kSensor) return (int8_t)i;
  }
  for (uint8_t i = 0; i < kMaxEntries; i++) {
    if (entries_[i].used) return (int8_t)i;
  }
  return -1;
}

bool ControlHubDashboard::isStale(const Entry &e) const {
  // Actuators reflect commanded state, not liveness, so they never go stale.
  if (e.kind == kActuator) return false;
  return (millis() - e.lastSeenMs) > kStaleMs;
}

uint8_t ControlHubDashboard::countKind(EntryKind kind) const {
  uint8_t c = 0;
  for (uint8_t i = 0; i < kMaxEntries; i++) {
    if (entries_[i].used && entries_[i].kind == kind) c++;
  }
  return c;
}

uint8_t ControlHubDashboard::visibleEntries(int8_t *out) const {
  int8_t idx[kMaxEntries];
  uint8_t c = 0;
  for (uint8_t i = 0; i < kMaxEntries; i++) {
    if (entries_[i].used) idx[c++] = (int8_t)i;
  }
  // Insertion sort by lastSeen, newest first.
  for (uint8_t i = 1; i < c; i++) {
    int8_t v = idx[i];
    unsigned long lv = entries_[v].lastSeenMs;
    int j = (int)i - 1;
    while (j >= 0 && entries_[idx[j]].lastSeenMs < lv) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
  uint8_t n = (c < kVisibleCards) ? c : kVisibleCards;
  for (uint8_t i = 0; i < n; i++) out[i] = idx[i];
  return n;
}

void ControlHubDashboard::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "RelayOps WiFi Control Hub") &&
           (CrowDisplay::canvas() != nullptr);
  if (!ready_) return;
  paintChrome();
  repaint();
}

void ControlHubDashboard::onSensor(const SensorReading &reading) {
  int8_t idx = findOrAddEntry(reading.nodeId, kSensor);
  if (idx < 0) return;
  Entry &n = entries_[idx];
  n.lastSeenMs = millis();
  n.rssi = reading.rssi;
  if (!reading.presenceOnly) {
    n.hasTelemetry = true;
    n.tempC = reading.temperatureC;
    n.humidityPct = reading.humidityPct;
    n.batteryPct = reading.batteryPct;
    n.motion = reading.motion;
    n.tempHist[n.histHead] = reading.temperatureC;
    n.histHead = (n.histHead + 1) % kHist;
    if (n.histCount < kHist) n.histCount++;
  }
  lastSensor_ = idx;
  sensorEvents_++;
  dirty_ = true;
}

void ControlHubDashboard::onDevice(const ControlDevice &device) {
  int8_t idx = findOrAddEntry(device.deviceId, kActuator);
  if (idx < 0) return;
  Entry &e = entries_[idx];
  e.host = device.host;
  e.pin = device.pin;
  e.on = device.state;
  e.online = device.online;
  e.lastSeenMs = millis();
  dirty_ = true;
}

void ControlHubDashboard::onEvent(const String &message) {
  banner_ = message;
  dirty_ = true;
}

bool ControlHubDashboard::takePendingToggle(String &deviceId, bool &desiredOn) {
  if (!hasPending_) return false;
  deviceId = pendingId_;
  desiredOn = pendingOn_;
  hasPending_ = false;
  return true;
}

void ControlHubDashboard::tick() {
  if (!ready_) return;

  int16_t tx, ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  if (touched && !wasTouched_) {
    int8_t vis[kVisibleCards];
    uint8_t vc = visibleEntries(vis);
    int8_t hit = -1;
    for (uint8_t s = 0; s < vc; s++) {
      int16_t y = cardYPos(s);
      if (tx >= kCardX && tx <= kCardX + kCardW && ty >= y && ty <= y + kCardH) {
        hit = vis[s];
        break;
      }
    }
    if (hit >= 0) {
      pinned_ = hit;
      if (entries_[hit].kind == kActuator) {
        // Queue the toggle; the sketch drains it and reports the new state
        // back through onDevice(), which is what actually flips the tile.
        pendingId_ = entries_[hit].name;
        pendingOn_ = !entries_[hit].on;
        hasPending_ = true;
      }
    } else {
      pinned_ = -1;  // tap on empty space unpins
    }
    dirty_ = true;
  }
  wasTouched_ = touched;

  if (dirty_) {
    repaint();
    dirty_ = false;
    return;
  }
  // No data change: keep the clock / heap / uptime / node ages live.
  static Throttle clockRefresh(1000);
  if (clockRefresh.ready()) {
    drawHeaderStatus();
    drawRoster();
    drawFooter();
  }
}

void ControlHubDashboard::paintChrome() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);
  towerIcon(g, 18, 20, kAccent);
  text(g, 58, 14, "RELAYOPS", fontL(), kTextHi, kLeft);
  text(g, 58, 44, "WIFI CONTROL HUB", fontS(), kTextMut, kLeft);
  text(g, kCardX + 4, 86, "SENSORS & DEVICES", fontS(), kTextMut, kLeft);
}

void ControlHubDashboard::repaint() {
  if (CrowDisplay::canvas() == nullptr) return;
  drawHeaderStatus();
  drawRoster();
  drawBanner();
  drawDetail();
  drawStats();
  drawSparkline();
  drawFooter();
}

void ControlHubDashboard::drawHeaderStatus() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(520, 0, kScreenW - 520, kHeaderH - 2, kSurface);

  char clock[28];
  snprintf(clock, sizeof(clock), "UP %lus", (unsigned long)(millis() / 1000));
  char heap[20];
  snprintf(heap, sizeof(heap), "%luK", (unsigned long)(ESP.getFreeHeap() / 1024));

  auto pw = [&](const char *s) { return (int16_t)(textWidth(g, s, fontS()) + 24); };
  int16_t gap = 10;
  int16_t total = pw(kLinkLabel) + gap + pw(heap) + gap + pw(clock);
  int16_t x = kScreenW - 16 - total;
  int16_t y = 22;

  x += pill(g, x, y, kLinkLabel, fontS(), kLinkAccent ? kBg : kTextHi,
            kLinkAccent ? kAccent : kSurfaceHi) + gap;
  x += pill(g, x, y, heap, fontS(), kTextHi, kSurfaceHi) + gap;
  pill(g, x, y, clock, fontS(), kTextHi, kSurfaceHi);
}

void ControlHubDashboard::drawRoster() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  // Clear the roster column (below the label) so vanished entries go away.
  g->fillRect(0, 104, 360, kFooterY - 104, kBg);

  int8_t vis[kVisibleCards];
  uint8_t vc = visibleEntries(vis);
  for (uint8_t s = 0; s < kVisibleCards; s++) {
    drawCard(s, s < vc ? vis[s] : -1);
  }
}

void ControlHubDashboard::drawCard(uint8_t slot, int8_t entryIdx) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || entryIdx < 0) return;

  const Entry &e = entries_[entryIdx];
  int16_t y = cardYPos(slot);
  bool active = (entryIdx == activeEntry());
  bool stale = isStale(e);

  uint16_t fill = active ? kSurfaceHi : kSurface;
  panel(g, kCardX, y, kCardW, kCardH, 10, fill, active ? 2 : 0, kAccent);

  if (e.kind == kActuator) {
    drawActuatorCard(y, e, active, stale);
  } else {
    drawSensorCard(y, e, active, stale);
  }
}

void ControlHubDashboard::drawSensorCard(int16_t y, const Entry &n, bool active, bool stale) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  (void)active;

  uint16_t dotColor;
  if (stale) {
    dotColor = kLine;
  } else if (!n.hasTelemetry) {
    dotColor = kAccent;  // presence: online, no telemetry
  } else {
    dotColor = (n.batteryPct < 35.0f || n.tempC > 35.0f)
                   ? kRed
                   : (n.batteryPct < 55.0f || n.tempC > 27.0f) ? kAmber : kGreen;
  }
  statusDot(g, kCardX + 24, y + 22, 7, dotColor);

  uint16_t nameColor = stale ? kTextMut : kTextHi;
  text(g, kCardX + 42, y + 8, fit(g, n.name, fontL(), 180).c_str(), fontL(), nameColor, kLeft);

  char age[10];
  snprintf(age, sizeof(age), "%lus", (unsigned long)((millis() - n.lastSeenMs) / 1000));
  text(g, kCardX + kCardW - 12, y + 10, age, fontS(), kTextMut, kRight);

  if (n.hasTelemetry) {
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%.1fC", n.tempC);
    text(g, kCardX + 42, y + 40, tbuf, fontS(), stale ? kTextMut : kTextHi, kLeft);
    hBar(g, kCardX + 116, y + 44, 104, 8, n.batteryPct / 100.0f, batteryColor(n.batteryPct));
    char bbuf[8];
    snprintf(bbuf, sizeof(bbuf), "%d%%", (int)(n.batteryPct + 0.5f));
    text(g, kCardX + 228, y + 40, bbuf, fontS(), kTextMut, kLeft);
  } else {
    text(g, kCardX + 42, y + 40, "online", fontS(), kTextMut, kLeft);
  }

  signalBars(g, kCardX + kCardW - 54, y + kCardH - 12, stale ? 0 : rssiLevel(n.rssi),
             stale ? kLine : kAccent);
}

void ControlHubDashboard::drawActuatorCard(int16_t y, const Entry &e, bool active, bool stale) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  (void)active;
  (void)stale;

  uint16_t dot = !e.online ? kAmber : (e.on ? kGreen : kTextMut);
  statusDot(g, kCardX + 24, y + 22, 7, dot);

  text(g, kCardX + 42, y + 8, fit(g, e.name, fontL(), 150).c_str(), fontL(), kTextHi, kLeft);

  char pinbuf[12];
  snprintf(pinbuf, sizeof(pinbuf), "GPIO %u", (unsigned)e.pin);
  text(g, kCardX + kCardW - 12, y + 10, pinbuf, fontS(), kTextMut, kRight);

  // Row 2: ON/OFF chip + target host.
  const char *st = e.on ? "ON" : "OFF";
  uint16_t pillFill = e.on ? kGreen : kSurfaceHi;
  uint16_t pillText = e.on ? kBg : kTextMut;
  int16_t pwid = pill(g, kCardX + 42, y + 38, st, fontS(), pillText, pillFill);
  int16_t hx = kCardX + 42 + pwid + 10;
  text(g, hx, y + 42, fit(g, e.host, fontS(), kCardX + kCardW - 12 - hx).c_str(), fontS(),
       kTextMut, kLeft);
}

void ControlHubDashboard::drawBanner() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  panel(g, kRX, kBannerY, kRW, kBannerH, 8, kAccent);
  const char *label = "HUB";
  text(g, kRX + 16, kBannerY + 13, label, fontS(), kBg, kLeft);
  int16_t detailX = kRX + 16 + textWidth(g, label, fontS()) + 18;
  text(g, detailX, kBannerY + 13, fit(g, banner_, fontS(), kRX + kRW - 12 - detailX).c_str(),
       fontS(), kBg, kLeft);
}

void ControlHubDashboard::drawDetail() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(kRX, 140, kRW, 200, kBg);

  int8_t a = activeEntry();
  int16_t cx = kRX + kRW / 2;
  if (a < 0) {
    text(g, cx, 220, "waiting for sensors & devices...", fontM(), kTextMut, kCenter);
    return;
  }

  const Entry &e = entries_[a];

  if (e.kind == kActuator) {
    uint16_t color = !e.online ? kAmber : (e.on ? kGreen : kTextMut);
    text(g, cx, 176, e.on ? "ON" : "OFF", fontXL(), color, kCenter);
    text(g, cx, 224, fit(g, e.name, fontM(), kRW - 40).c_str(), fontM(), kTextHi, kCenter);
    char tgt[64];
    snprintf(tgt, sizeof(tgt), "GPIO %u  @  %s", (unsigned)e.pin, e.host.c_str());
    text(g, cx, 256, fit(g, tgt, fontS(), kRW - 40).c_str(), fontS(), kTextMut, kCenter);
    text(g, cx, 288, "tap tile to toggle", fontS(), kTextMut, kCenter);
    return;
  }

  if (!e.hasTelemetry) {
    text(g, cx, 190, "PRESENCE", fontL(), kAccent, kCenter);
    text(g, cx, 224, fit(g, e.name, fontM(), kRW - 40).c_str(), fontM(), kTextHi, kCenter);
    text(g, cx, 256, "node online - no telemetry", fontS(), kTextMut, kCenter);
    return;
  }

  int16_t cxA = kRX + 150;
  int16_t cxB = kRX + kRW - 150;

  arcGauge(g, cxA, kGaugeCy, kGaugeROut, kGaugeRIn, e.batteryPct / 100.0f, batteryColor(e.batteryPct));
  char b[8];
  snprintf(b, sizeof(b), "%d%%", (int)(e.batteryPct + 0.5f));
  text(g, cxA, kGaugeCy - 26, b, fontXL(), kTextHi, kCenter);
  text(g, cxA, kGaugeCy + 24, "BATTERY", fontS(), kTextMut, kCenter);

  arcGauge(g, cxB, kGaugeCy, kGaugeROut, kGaugeRIn, e.tempC / 50.0f, tempColor(e.tempC));
  char t[10];
  snprintf(t, sizeof(t), "%.1f", e.tempC);
  text(g, cxB, kGaugeCy - 26, t, fontXL(), kTextHi, kCenter);
  text(g, cxB, kGaugeCy + 24, "TEMPERATURE C", fontS(), kTextMut, kCenter);
}

void ControlHubDashboard::drawStats() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(kRX, kStatsY, kRW, 52, kBg);

  int8_t a = activeEntry();
  if (a < 0) return;
  const Entry &n = entries_[a];
  if (n.kind != kSensor || !n.hasTelemetry) return;

  text(g, kRX, kStatsY, "HUMIDITY", fontS(), kTextMut, kLeft);
  hBar(g, kRX, kStatsY + 24, 200, 12, n.humidityPct / 100.0f, kAccent);
  char h[10];
  snprintf(h, sizeof(h), "%d%%", (int)(n.humidityPct + 0.5f));
  text(g, kRX + 212, kStatsY + 22, h, fontS(), kTextHi, kLeft);

  int16_t sx = kRX + 300;
  text(g, sx, kStatsY, "SIGNAL", fontS(), kTextMut, kLeft);
  signalBars(g, sx, kStatsY + 40, rssiLevel(n.rssi), kAccent);
  char r[14];
  snprintf(r, sizeof(r), "%d dBm", n.rssi);
  text(g, sx + 60, kStatsY + 22, r, fontS(), kTextHi, kLeft);

  int16_t mx = kRX + kRW - 130;
  if (n.motion) {
    pill(g, mx, kStatsY + 16, "MOTION", fontS(), kBg, kAmber);
  } else {
    pill(g, mx, kStatsY + 16, "IDLE", fontS(), kTextMut, kSurfaceHi);
  }
}

void ControlHubDashboard::drawSparkline() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  panel(g, kRX, kSparkY, kRW, kSparkH, 10, kSurface);
  text(g, kRX + 14, kSparkY + 12, "TEMPERATURE TREND", fontS(), kTextMut, kLeft);

  int8_t a = activeEntry();
  if (a >= 0 && entries_[a].kind == kSensor && entries_[a].hasTelemetry &&
      entries_[a].histCount >= 2) {
    const Entry &n = entries_[a];
    uint16_t tail = (n.histHead + kHist - n.histCount) % kHist;
    sparkline(g, kRX + 14, kSparkY + 36, kRW - 28, kSparkH - 50,
              n.tempHist, n.histCount, tail, 0.0f, 0.0f, kAccent, kSurfaceHi);
  } else {
    text(g, kRX + 14, kSparkY + 48, "select a sensor node for its trend", fontS(), kTextMut, kLeft);
  }
}

void ControlHubDashboard::drawFooter() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, kLine);

  statusDot(g, 24, kFooterY + 30, 5, kAccent);
  text(g, 40, kFooterY + 22, fit(g, banner_, fontS(), 560).c_str(), fontS(), kTextHi, kLeft);

  char buf[56];
  snprintf(buf, sizeof(buf), "SENSORS %u   DEVICES %u   UP %lus",
           (unsigned)countKind(kSensor), (unsigned)countKind(kActuator),
           (unsigned long)(millis() / 1000));
  text(g, kScreenW - 16, kFooterY + 22, buf, fontS(), kTextMut, kRight);
}

#else  // display disabled: no-op so the sketch still runs Serial-only

void ControlHubDashboard::begin() {}
void ControlHubDashboard::onSensor(const SensorReading &) {}
void ControlHubDashboard::onDevice(const ControlDevice &) {}
void ControlHubDashboard::onEvent(const String &) {}
void ControlHubDashboard::tick() {}
bool ControlHubDashboard::takePendingToggle(String &, bool &) { return false; }

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
