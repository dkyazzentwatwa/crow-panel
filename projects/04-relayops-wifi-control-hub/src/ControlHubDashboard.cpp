#include "ControlHubDashboard.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>
#include <math.h>

using namespace Widgets;

namespace {

// --- Layout (1024 x 600). Content lives between the header (72px) and the
// tab strip (kChromeTabY = 536). ---
constexpr int16_t kW = 1024;
constexpr int16_t kContentTop = 84;

// The hub is a Wi-Fi server, not a radio; the link pill reflects the flag.
#if USE_WIFI
const char *kLinkLabel = "WIFI";
constexpr bool kLinkAccent = true;
#else
const char *kLinkLabel = "MOCK";
constexpr bool kLinkAccent = false;
#endif

const char *const kTabLabels[4] = {"DEVICES", "SENSORS", "WORLD", "EVENTS"};

// Device grid: 2 columns, up to 4 rows (8 devices).
constexpr int16_t kGridX = 20;
constexpr int16_t kColGap = 20;
constexpr int16_t kCardW = (kW - 2 * kGridX - kColGap) / 2;  // 482
constexpr int16_t kCardH = 100;
constexpr int16_t kRowGap = 14;
constexpr uint8_t kGridCols = 2;
constexpr uint8_t kGridSlots = 8;

int16_t devCardX(uint8_t slot) { return kGridX + (slot % kGridCols) * (kCardW + kColGap); }
int16_t devCardY(uint8_t slot) { return kContentTop + (slot / kGridCols) * (kCardH + kRowGap); }

// DETAIL action buttons.
struct Rect { int16_t x, y, w, h; };
const Rect kDetOn = {20, 452, 210, 60};
const Rect kDetOff = {244, 452, 210, 60};
const Rect kDetToggle = {468, 452, 230, 60};
const Rect kDetBack = {760, 452, 244, 60};

// WORLD refresh button.
const Rect kWorldRefresh = {20, 490, 240, 40};

HubScreen screenForTab(int8_t tab) {
  switch (tab) {
    case 0: return HUB_DEVICES;
    case 1: return HUB_SENSORS;
    case 2: return HUB_WORLD;
    case 3: return HUB_EVENTS;
    default: return HUB_DEVICES;
  }
}

uint8_t tabForScreen(HubScreen s) {
  switch (s) {
    case HUB_DEVICES:
    case HUB_DETAIL: return 0;
    case HUB_SENSORS: return 1;
    case HUB_WORLD: return 2;
    case HUB_EVENTS: return 3;
    default: return 0;
  }
}

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

// A relay-style toggle glyph (drawn only; the whole card is the hit target).
void drawToggle(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
  int16_t r = h / 2;
  g->fillRoundRect(x, y, w, h, r, on ? kGreen : kSurfaceHi);
  int16_t knobR = r - 3;
  int16_t kcx = on ? (x + w - r) : (x + r);
  g->fillCircle(kcx, y + r, knobR, on ? kBg : kTextMut);
  if (on) {
    text(g, x + 12, y + (h - 13) / 2, "ON", fontS(), kBg, kLeft);
  } else {
    text(g, x + w - 12, y + (h - 13) / 2, "OFF", fontS(), kTextMut, kRight);
  }
}

String deviceUrl(const String &host, const String &path, uint8_t pin, bool state) {
  return "http://" + host + path + "?pin=" + String(pin) + "&state=" + (state ? "1" : "0");
}

String ageStr(unsigned long ms) {
  unsigned long s = (millis() - ms) / 1000;
  if (s < 90) return String(s) + "s";
  return String(s / 60) + "m";
}

}  // namespace

// ---------------------------------------------------------------------------
// Model helpers
// ---------------------------------------------------------------------------

int8_t ControlHubDashboard::findOrAddSensor_(const String &name) {
  for (uint8_t i = 0; i < kMaxSensors; i++) {
    if (sensors_[i].used && sensors_[i].name == name) return (int8_t)i;
  }
  for (uint8_t i = 0; i < kMaxSensors; i++) {
    if (!sensors_[i].used) {
      sensors_[i] = Sensor();
      sensors_[i].used = true;
      sensors_[i].name = name;
      return (int8_t)i;
    }
  }
  // Full: evict the stalest.
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < kMaxSensors; i++) {
    if (sensors_[i].lastSeenMs < sensors_[oldest].lastSeenMs) oldest = i;
  }
  sensors_[oldest] = Sensor();
  sensors_[oldest].used = true;
  sensors_[oldest].name = name;
  return (int8_t)oldest;
}

int8_t ControlHubDashboard::newestSensor_() const {
  int8_t best = -1;
  unsigned long newest = 0;
  for (uint8_t i = 0; i < kMaxSensors; i++) {
    if (sensors_[i].used && (best < 0 || sensors_[i].lastSeenMs >= newest)) {
      best = (int8_t)i;
      newest = sensors_[i].lastSeenMs;
    }
  }
  return best;
}

int8_t ControlHubDashboard::activeSensor_() const {
  if (pinnedSensor_ >= 0 && sensors_[pinnedSensor_].used) return pinnedSensor_;
  return newestSensor_();
}

bool ControlHubDashboard::sensorStale_(const Sensor &s) const {
  return (millis() - s.lastSeenMs) > kStaleMs;
}

uint8_t ControlHubDashboard::visibleSensors_(int8_t *out, uint8_t max) const {
  int8_t idx[kMaxSensors];
  uint8_t c = 0;
  for (uint8_t i = 0; i < kMaxSensors; i++) {
    if (sensors_[i].used) idx[c++] = (int8_t)i;
  }
  // Insertion sort by lastSeen, newest first.
  for (uint8_t i = 1; i < c; i++) {
    int8_t v = idx[i];
    unsigned long lv = sensors_[v].lastSeenMs;
    int j = (int)i - 1;
    while (j >= 0 && sensors_[idx[j]].lastSeenMs < lv) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
  uint8_t n = (c < max) ? c : max;
  for (uint8_t i = 0; i < n; i++) out[i] = idx[i];
  return n;
}

int8_t ControlHubDashboard::findDevice_(const String &id) const {
  for (uint8_t i = 0; i < kMaxDevices; i++) {
    if (devices_[i].used && devices_[i].id == id) return (int8_t)i;
  }
  return -1;
}

uint8_t ControlHubDashboard::deviceOrder_(int8_t *out) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMaxDevices && n < kGridSlots; i++) {
    if (devices_[i].used) out[n++] = (int8_t)i;
  }
  return n;
}

uint8_t ControlHubDashboard::deviceCount_() const {
  uint8_t c = 0;
  for (uint8_t i = 0; i < kMaxDevices; i++)
    if (devices_[i].used) c++;
  return c;
}

uint8_t ControlHubDashboard::sensorCount_() const {
  uint8_t c = 0;
  for (uint8_t i = 0; i < kMaxSensors; i++)
    if (sensors_[i].used) c++;
  return c;
}

void ControlHubDashboard::markCommand_(int8_t devIdx, bool on) {
  if (devIdx < 0 || devIdx >= (int8_t)kMaxDevices) return;
  devices_[devIdx].lastCmd = on ? "SET ON" : "SET OFF";
  devices_[devIdx].lastCmdMs = millis();
  devices_[devIdx].awaitingResult = true;
}

// ---------------------------------------------------------------------------
// Lifecycle + inbound data
// ---------------------------------------------------------------------------

void ControlHubDashboard::begin() {
  // manualFlush=true: draws only touch the cached framebuffer until flush().
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "RelayOps WiFi Control Hub",
                              /*manualFlush=*/true) &&
           (CrowDisplay::canvas() != nullptr);
  if (!ready_) return;
  dirty_ = true;
  draw_();
  lastDrawMs_ = millis();
}

void ControlHubDashboard::onSensor(const SensorReading &reading) {
  int8_t idx = findOrAddSensor_(reading.nodeId);
  if (idx < 0) return;
  Sensor &s = sensors_[idx];
  s.lastSeenMs = millis();
  s.rssi = reading.rssi;
  if (!reading.presenceOnly) {
    s.hasTelemetry = true;
    s.tempC = reading.temperatureC;
    s.humidityPct = reading.humidityPct;
    s.batteryPct = reading.batteryPct;
    s.motion = reading.motion;
    s.tempHist[s.histHead] = reading.temperatureC;
    s.histHead = (s.histHead + 1) % kHist;
    if (s.histCount < kHist) s.histCount++;
  }
  sensorEvents_++;
  dirty_ = true;
}

void ControlHubDashboard::onDevice(const ControlDevice &device) {
  int8_t idx = findDevice_(device.deviceId);
  if (idx < 0) {
    for (uint8_t i = 0; i < kMaxDevices; i++) {
      if (!devices_[i].used) {
        idx = (int8_t)i;
        devices_[i] = Device();
        devices_[i].used = true;
        devices_[i].id = device.deviceId;
        break;
      }
    }
  }
  if (idx < 0) return;  // registry full
  Device &d = devices_[idx];
  d.host = device.host;
  d.path = device.path;
  d.pin = device.pin;
  d.online = device.online;
  d.state = device.state;
  d.lastSeenMs = millis();
  if (d.awaitingResult) {
    d.lastResult = device.online ? "ok" : "unreachable";
    d.awaitingResult = false;
  }
  if (selDevice_ < 0) selDevice_ = idx;
  dirty_ = true;
}

void ControlHubDashboard::onEvent(const String &message) {
  events_[eventHead_] = message;
  eventMs_[eventHead_] = millis();
  eventHead_ = (eventHead_ + 1) % kEventCap;
  if (eventCount_ < kEventCap) eventCount_++;
  dirty_ = true;
}

void ControlHubDashboard::onWorldFeeds(const WorldFeeds &feeds) {
  world_ = feeds;
  dirty_ = true;
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

HubUiEvent ControlHubDashboard::tick() {
  if (!ready_) return HubUiEvent();
  touch_.tick();
  HubUiEvent ev = handleTouch_();
  bool periodic = (millis() - lastDrawMs_) >= 1000;  // keep ages/heap/clock live
  if (dirty_ || periodic) {
    draw_();
    dirty_ = false;
    lastDrawMs_ = millis();
  }
  return ev;
}

HubUiEvent ControlHubDashboard::handleTouch_() {
  if (!touch_.releasedEdge()) return HubUiEvent();
  int16_t x = touch_.releaseX();
  int16_t y = touch_.releaseY();

  // Bottom tab strip is global to every screen.
  int8_t tab = tabHit(x, y, 4);
  if (tab >= 0) {
    HubScreen target = screenForTab(tab);
    if (target != screen_) {
      screen_ = target;
      dirty_ = true;
    }
    return HubUiEvent();
  }

  switch (screen_) {
    case HUB_DEVICES: return touchDevices_(x, y);
    case HUB_DETAIL: return touchDetail_(x, y);
    case HUB_SENSORS: return touchSensors_(x, y);
    case HUB_WORLD: return touchWorld_(x, y);
    default: return HubUiEvent();
  }
}

HubUiEvent ControlHubDashboard::touchDevices_(int16_t x, int16_t y) {
  int8_t order[kGridSlots];
  uint8_t n = deviceOrder_(order);
  for (uint8_t slot = 0; slot < n; slot++) {
    int16_t cx = devCardX(slot), cy = devCardY(slot);
    // The DETAILS button opens the detail screen; the rest of the card toggles.
    int16_t dbx = cx + kCardW - 18 - 128, dby = cy + 62, dbw = 128, dbh = 32;
    if (hitRect(x, y, dbx, dby, dbw, dbh)) {
      selDevice_ = order[slot];
      screen_ = HUB_DETAIL;
      dirty_ = true;
      return HubUiEvent();
    }
    if (hitRect(x, y, cx, cy, kCardW, kCardH)) {
      Device &d = devices_[order[slot]];
      HubUiEvent ev;
      ev.type = kHubSetDevice;
      ev.deviceId = d.id;
      ev.on = !d.state;
      markCommand_(order[slot], ev.on);
      dirty_ = true;
      return ev;
    }
  }
  return HubUiEvent();
}

HubUiEvent ControlHubDashboard::touchDetail_(int16_t x, int16_t y) {
  if (hitRect(x, y, kDetBack.x, kDetBack.y, kDetBack.w, kDetBack.h)) {
    screen_ = HUB_DEVICES;
    dirty_ = true;
    return HubUiEvent();
  }
  if (selDevice_ < 0 || selDevice_ >= (int8_t)kMaxDevices || !devices_[selDevice_].used) {
    return HubUiEvent();
  }
  Device &d = devices_[selDevice_];
  int8_t want = -1;  // 1 on, 0 off
  if (hitRect(x, y, kDetOn.x, kDetOn.y, kDetOn.w, kDetOn.h)) want = 1;
  else if (hitRect(x, y, kDetOff.x, kDetOff.y, kDetOff.w, kDetOff.h)) want = 0;
  else if (hitRect(x, y, kDetToggle.x, kDetToggle.y, kDetToggle.w, kDetToggle.h)) want = d.state ? 0 : 1;
  if (want < 0) return HubUiEvent();

  HubUiEvent ev;
  ev.type = kHubSetDevice;
  ev.deviceId = d.id;
  ev.on = (want == 1);
  markCommand_(selDevice_, ev.on);
  dirty_ = true;
  return ev;
}

HubUiEvent ControlHubDashboard::touchSensors_(int16_t x, int16_t y) {
  int8_t vis[4];
  uint8_t n = visibleSensors_(vis, 4);
  for (uint8_t slot = 0; slot < n; slot++) {
    int16_t cy = kContentTop + slot * (84 + 10);
    if (hitRect(x, y, 20, cy, 360, 84)) {
      pinnedSensor_ = vis[slot];
      dirty_ = true;
      return HubUiEvent();
    }
  }
  return HubUiEvent();
}

HubUiEvent ControlHubDashboard::touchWorld_(int16_t x, int16_t y) {
  if (hitRect(x, y, kWorldRefresh.x, kWorldRefresh.y, kWorldRefresh.w, kWorldRefresh.h)) {
    HubUiEvent ev;
    ev.type = kHubRefreshWorld;
    dirty_ = true;
    return ev;
  }
  return HubUiEvent();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ControlHubDashboard::draw_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  drawHeader_();
  switch (screen_) {
    case HUB_DEVICES: drawDevices_(); break;
    case HUB_DETAIL: drawDetail_(); break;
    case HUB_SENSORS: drawSensors_(); break;
    case HUB_WORLD: drawWorld_(); break;
    case HUB_EVENTS: drawEvents_(); break;
    default: break;
  }
  drawTabs_();
  CrowDisplay::flush();
}

void ControlHubDashboard::drawHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  char sub[48];
  switch (screen_) {
    case HUB_DEVICES:
      snprintf(sub, sizeof(sub), "DEVICE CONTROL   %u devices", (unsigned)deviceCount_());
      break;
    case HUB_DETAIL: snprintf(sub, sizeof(sub), "DEVICE DETAIL"); break;
    case HUB_SENSORS:
      snprintf(sub, sizeof(sub), "SENSOR NODES   %u live", (unsigned)sensorCount_());
      break;
    case HUB_WORLD: snprintf(sub, sizeof(sub), "WORLD FEED   weather / quake / aurora / air"); break;
    case HUB_EVENTS: snprintf(sub, sizeof(sub), "EVENT LOG   %u recent", (unsigned)eventCount_); break;
    default: sub[0] = '\0'; break;
  }
  headerBar(g, "RELAYOPS", sub, nullptr);

  // Right-aligned live status pills: link / heap / uptime.
  char heap[16], clock[16];
  snprintf(heap, sizeof(heap), "%luK", (unsigned long)(ESP.getFreeHeap() / 1024));
  snprintf(clock, sizeof(clock), "UP %lus", (unsigned long)(millis() / 1000));
  auto pw = [&](const char *s) { return (int16_t)(textWidth(g, s, fontS()) + 24); };
  const int16_t gap = 8;
  int16_t total = pw(kLinkLabel) + gap + pw(heap) + gap + pw(clock);
  int16_t x = kW - 24 - total;
  int16_t y = (kChromeHeaderH - 28) / 2;
  x += pill(g, x, y, kLinkLabel, fontS(), kLinkAccent ? kBg : kTextHi,
            kLinkAccent ? kAccent : kSurfaceHi) + gap;
  x += pill(g, x, y, heap, fontS(), kTextHi, kSurfaceHi) + gap;
  pill(g, x, y, clock, fontS(), kTextHi, kSurfaceHi);
}

void ControlHubDashboard::drawTabs_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  tabBar(g, kTabLabels, 4, tabForScreen(screen_));
}

// --- DEVICES -----------------------------------------------------------------

void ControlHubDashboard::drawDevices_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  int8_t order[kGridSlots];
  uint8_t n = deviceOrder_(order);
  if (n == 0) {
    text(g, kW / 2, 260, "no controllable devices", fontL(), kTextMut, kCenter);
    text(g, kW / 2, 296, "seed config/Devices.h, POST /register, or run `set`", fontS(),
         kTextMut, kCenter);
    return;
  }
  for (uint8_t slot = 0; slot < n; slot++) drawDeviceCard_(slot, order[slot]);
}

void ControlHubDashboard::drawDeviceCard_(uint8_t slot, int8_t devIdx) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || devIdx < 0) return;
  const Device &d = devices_[devIdx];
  int16_t x = devCardX(slot), y = devCardY(slot);
  bool sel = (devIdx == selDevice_);

  panel(g, x, y, kCardW, kCardH, 12, sel ? kSurfaceHi : kSurface, sel ? 2 : 1,
        sel ? kAccent : kLine);

  uint16_t dot = !d.online ? kAmber : (d.state ? kGreen : kTextMut);
  statusDot(g, x + 24, y + 26, 7, dot);
  text(g, x + 42, y + 12, fit(g, d.id, fontL(), 240).c_str(), fontL(), kTextHi, kLeft);

  char pinbuf[12];
  snprintf(pinbuf, sizeof(pinbuf), "GPIO %u", (unsigned)d.pin);
  int16_t ppw = textWidth(g, pinbuf, fontS()) + 24;
  pill(g, x + kCardW - 18 - ppw, y + 12, pinbuf, fontS(), kTextHi, kSurfaceHi);

  text(g, x + 42, y + 42, fit(g, d.host, fontS(), kCardW - 60).c_str(), fontS(), kTextMut, kLeft);

  drawToggle(g, x + 18, y + 64, 96, 28, d.state);
  touchButton(g, x + kCardW - 18 - 128, y + 62, 128, 32, "DETAILS", false);
}

// --- DETAIL ------------------------------------------------------------------

void ControlHubDashboard::drawDetail_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  panel(g, 20, kContentTop, kW - 40, 344, 16, kSurface, 1, kLine);

  if (selDevice_ < 0 || selDevice_ >= (int8_t)kMaxDevices || !devices_[selDevice_].used) {
    text(g, kW / 2, 236, "no device selected", fontL(), kTextMut, kCenter);
    touchButton(g, kDetBack.x, kDetBack.y, kDetBack.w, kDetBack.h, "< DEVICES", false);
    return;
  }
  const Device &d = devices_[selDevice_];

  text(g, 44, 104, fit(g, d.id, fontL(), 520).c_str(), fontL(), kTextHi, kLeft);

  // Online pill, top-right of the card.
  const char *onl = d.online ? "ONLINE" : "OFFLINE";
  int16_t opw = textWidth(g, onl, fontS()) + 24;
  pill(g, kW - 44 - opw, 102, onl, fontS(), kBg, d.online ? kGreen : kAmber);

  // Big commanded state.
  uint16_t stateColor = !d.online ? kAmber : (d.state ? kGreen : kTextMut);
  statusDot(g, 58, 168, 12, stateColor);
  text(g, 84, 146, d.state ? "ON" : "OFF", fontXL(), stateColor, kLeft);
  char pinbuf[24];
  snprintf(pinbuf, sizeof(pinbuf), "GPIO %u", (unsigned)d.pin);
  text(g, 44, 204, pinbuf, fontM(), kTextMut, kLeft);

  g->drawFastHLine(44, 236, kW - 88, kLine);

  text(g, 44, 246, "HTTP TARGET", fontS(), kTextMut, kLeft);
  String url = deviceUrl(d.host, d.path, d.pin, d.state);
  text(g, 44, 270, fit(g, url, fontM(), kW - 100).c_str(), fontM(), kTextHi, kLeft);

  text(g, 44, 316, "LAST COMMAND", fontS(), kTextMut, kLeft);
  String last;
  if (d.lastCmd.length() == 0) {
    last = "none yet";
  } else {
    last = d.lastCmd;
    if (d.awaitingResult) last += " -> pending";
    else if (d.lastResult.length()) last += " -> " + d.lastResult;
    last += "   (" + ageStr(d.lastCmdMs) + " ago)";
  }
  text(g, 44, 340, fit(g, last, fontM(), kW - 100).c_str(), fontM(), kTextHi, kLeft);

  // Explicit controls.
  touchButton(g, kDetOn.x, kDetOn.y, kDetOn.w, kDetOn.h, "ON", d.state, kGreen);
  touchButton(g, kDetOff.x, kDetOff.y, kDetOff.w, kDetOff.h, "OFF", !d.state);
  touchButton(g, kDetToggle.x, kDetToggle.y, kDetToggle.w, kDetToggle.h, "TOGGLE", false);
  touchButton(g, kDetBack.x, kDetBack.y, kDetBack.w, kDetBack.h, "< DEVICES", false);
}

// --- SENSORS -----------------------------------------------------------------

void ControlHubDashboard::drawSensors_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  if (sensorCount_() == 0) {
    text(g, kW / 2, 250, "waiting for sensor nodes", fontL(), kTextMut, kCenter);
    text(g, kW / 2, 286, "inject one with `feed SENSOR,ATTIC,29.5,40,88,0,-58` or POST /sensor",
         fontS(), kTextMut, kCenter);
    return;
  }
  drawSensorList_();
  drawSensorDetail_();
  drawSensorSpark_();
}

void ControlHubDashboard::drawSensorList_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  int8_t vis[4];
  uint8_t n = visibleSensors_(vis, 4);
  int8_t active = activeSensor_();
  for (uint8_t slot = 0; slot < n; slot++) {
    const Sensor &s = sensors_[vis[slot]];
    int16_t y = kContentTop + slot * (84 + 10);
    bool sel = (vis[slot] == active);
    bool stale = sensorStale_(s);
    panel(g, 20, y, 360, 84, 10, sel ? kSurfaceHi : kSurface, sel ? 2 : 1,
          sel ? kAccent : kLine);

    uint16_t dot;
    if (stale) dot = kLine;
    else if (!s.hasTelemetry) dot = kAccent;
    else dot = (s.batteryPct < 35.0f || s.tempC > 35.0f)
                   ? kRed
                   : (s.batteryPct < 55.0f || s.tempC > 27.0f) ? kAmber : kGreen;
    statusDot(g, 44, y + 24, 7, dot);
    text(g, 62, y + 10, fit(g, s.name, fontL(), 210).c_str(), fontL(),
         stale ? kTextMut : kTextHi, kLeft);
    text(g, 368, y + 12, ageStr(s.lastSeenMs).c_str(), fontS(), kTextMut, kRight);

    if (s.hasTelemetry) {
      char tbuf[12];
      snprintf(tbuf, sizeof(tbuf), "%.1fC", s.tempC);
      text(g, 62, y + 44, tbuf, fontS(), stale ? kTextMut : kTextHi, kLeft);
      hBar(g, 130, y + 48, 96, 8, s.batteryPct / 100.0f, batteryColor(s.batteryPct));
      char bbuf[8];
      snprintf(bbuf, sizeof(bbuf), "%d%%", (int)(s.batteryPct + 0.5f));
      text(g, 234, y + 44, bbuf, fontS(), kTextMut, kLeft);
    } else {
      text(g, 62, y + 44, "presence only", fontS(), kTextMut, kLeft);
    }
    signalBars(g, 300, y + 84 - 12, stale ? 0 : rssiLevel(s.rssi), stale ? kLine : kAccent);
  }
}

void ControlHubDashboard::drawSensorDetail_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t x0 = 400, w0 = kW - x0 - 20;  // 604

  int8_t a = activeSensor_();
  if (a < 0) return;
  const Sensor &s = sensors_[a];

  char namebuf[40];
  snprintf(namebuf, sizeof(namebuf), "NODE %s", s.name.c_str());
  text(g, x0, 90, fit(g, namebuf, fontL(), w0 - 120).c_str(), fontL(), kTextHi, kLeft);
  text(g, x0 + w0, 92, ageStr(s.lastSeenMs).c_str(), fontS(), kTextMut, kRight);

  if (!s.hasTelemetry) {
    text(g, x0 + w0 / 2, 210, "PRESENCE", fontL(), kAccent, kCenter);
    text(g, x0 + w0 / 2, 246, "node online - no telemetry", fontS(), kTextMut, kCenter);
    return;
  }

  const int16_t cy = 196, rOut = 74, rIn = 57;
  const int16_t cxA = x0 + 150, cxB = x0 + w0 - 150;

  arcGauge(g, cxA, cy, rOut, rIn, s.batteryPct / 100.0f, batteryColor(s.batteryPct));
  char b[8];
  snprintf(b, sizeof(b), "%d%%", (int)(s.batteryPct + 0.5f));
  text(g, cxA, cy - 22, b, fontXL(), kTextHi, kCenter);
  text(g, cxA, cy + 22, "BATTERY", fontS(), kTextMut, kCenter);

  arcGauge(g, cxB, cy, rOut, rIn, s.tempC / 50.0f, tempColor(s.tempC));
  char t[10];
  snprintf(t, sizeof(t), "%.1f", s.tempC);
  text(g, cxB, cy - 22, t, fontXL(), kTextHi, kCenter);
  text(g, cxB, cy + 22, "TEMP C", fontS(), kTextMut, kCenter);

  // Stats row.
  const int16_t sy = 298;
  text(g, x0 + 20, sy, "HUMIDITY", fontS(), kTextMut, kLeft);
  hBar(g, x0 + 20, sy + 22, 190, 12, s.humidityPct / 100.0f, kAccent);
  char h[10];
  snprintf(h, sizeof(h), "%d%%", (int)(s.humidityPct + 0.5f));
  text(g, x0 + 220, sy + 20, h, fontS(), kTextHi, kLeft);

  int16_t sx = x0 + 320;
  text(g, sx, sy, "SIGNAL", fontS(), kTextMut, kLeft);
  signalBars(g, sx, sy + 38, rssiLevel(s.rssi), kAccent);
  char r[14];
  snprintf(r, sizeof(r), "%d dBm", s.rssi);
  text(g, sx + 60, sy + 20, r, fontS(), kTextHi, kLeft);

  if (s.motion) pill(g, x0 + w0 - 118, sy + 12, "MOTION", fontS(), kBg, kAmber);
  else pill(g, x0 + w0 - 118, sy + 12, "IDLE", fontS(), kTextMut, kSurfaceHi);
}

void ControlHubDashboard::drawSensorSpark_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t x0 = 400, w0 = kW - x0 - 20;
  panel(g, x0, 360, w0, 162, 12, kSurface, 1, kLine);
  text(g, x0 + 18, 372, "TEMPERATURE TREND", fontS(), kTextMut, kLeft);

  int8_t a = activeSensor_();
  if (a >= 0 && sensors_[a].hasTelemetry && sensors_[a].histCount >= 2) {
    const Sensor &s = sensors_[a];
    uint16_t tail = (s.histHead + kHist - s.histCount) % kHist;
    sparkline(g, x0 + 18, 400, w0 - 36, 108, s.tempHist, s.histCount, tail, 0.0f, 0.0f, kAccent,
              kSurfaceHi);
  } else {
    text(g, x0 + 18, 430, "history builds as the node reports in", fontS(), kTextMut, kLeft);
  }
}

// --- WORLD -------------------------------------------------------------------

void ControlHubDashboard::drawWorldCard_(int16_t x, int16_t y, int16_t w, int16_t h,
                                         const char *label, const String &big, const String &sub,
                                         const String &extra, bool valid, unsigned long ms,
                                         uint16_t accentColor) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  bool stale = !valid || (millis() - ms) > kWorldStaleMs;
  uint16_t bodyColor = stale ? kTextMut : kTextHi;
  uint16_t acc = stale ? kLine : accentColor;

  panel(g, x, y, w, h, 12, kSurface, 1, kLine);
  text(g, x + 18, y + 14, label, fontS(), acc, kLeft);
  text(g, x + 18, y + 42, fit(g, big, fontXL(), w - 36).c_str(), fontXL(), bodyColor, kLeft);
  text(g, x + 18, y + 96, fit(g, sub, fontM(), w - 36).c_str(), fontM(), kTextHi, kLeft);
  text(g, x + 18, y + 128, fit(g, extra, fontS(), w - 36).c_str(), fontS(), kTextMut, kLeft);
}

void ControlHubDashboard::drawWorld_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t cw = kCardW, chh = 190, gap = 14;
  const int16_t c0x = 20, c1x = 20 + cw + kColGap, r0y = kContentTop, r1y = kContentTop + chh + gap;

  // Weather.
  String wxBig = world_.weatherValid ? (String(world_.weather.tempC, 0) + "C") : "--";
  char wxEx[64] = "";
  if (world_.weatherValid) {
    snprintf(wxEx, sizeof(wxEx), "feels %.0f  wind %.0fkt  hi %.0f / lo %.0f", world_.weather.feelsC,
             world_.weather.windKt, world_.weather.hiC, world_.weather.loC);
  }
  drawWorldCard_(c0x, r0y, cw, chh, "WEATHER", wxBig, world_.weather.condition, wxEx,
                 world_.weatherValid, world_.weatherMs, kAccent);

  // Quake.
  String qBig = world_.quakeValid ? ("M" + String(world_.quake.mag, 1)) : "--";
  char qEx[72] = "";
  if (world_.quakeValid) {
    char depth[24] = "";
    if (!isnan(world_.quake.depthKm)) snprintf(depth, sizeof(depth), "depth %.0fkm  ", world_.quake.depthKm);
    char cnt[24] = "";
    if (world_.quake.count24h >= 0) snprintf(cnt, sizeof(cnt), " %d in 24h", world_.quake.count24h);
    snprintf(qEx, sizeof(qEx), "%s%s", depth, cnt);
  }
  drawWorldCard_(c1x, r0y, cw, chh, "EARTHQUAKE", qBig, world_.quake.place, qEx, world_.quakeValid,
                 world_.quakeMs, kAmber);

  // Aurora.
  String aBig = world_.auroraValid ? ("Kp " + String(world_.aurora.kp, 1)) : "--";
  const char *verdict = world_.aurora.verdict == kAuroraLikely ? "likely tonight"
                        : world_.aurora.verdict == kAuroraWatch ? "watch"
                                                                : "quiet";
  const char *trend = world_.aurora.trend > 0 ? "rising"
                      : world_.aurora.trend < 0 ? "falling"
                                                : "steady";
  char aEx[48];
  snprintf(aEx, sizeof(aEx), "%s  -  %s", verdict, trend);
  uint16_t aColor = world_.aurora.verdict == kAuroraLikely ? kGreen
                    : world_.aurora.verdict == kAuroraWatch ? kAmber : kTextMut;
  drawWorldCard_(c0x, r1y, cw, chh, "AURORA", aBig, world_.aurora.level, aEx, world_.auroraValid,
                 world_.auroraMs, aColor);
  // Kp trend arrow, top-right of the aurora card.
  if (world_.auroraValid) {
    int16_t ax = c0x + cw - 26, ay = r1y + 18;
    if (world_.aurora.trend > 0) g->fillTriangle(ax, ay, ax - 6, ay + 11, ax + 6, ay + 11, kGreen);
    else if (world_.aurora.trend < 0)
      g->fillTriangle(ax, ay + 11, ax - 6, ay, ax + 6, ay, kAmber);
    else g->fillRect(ax - 6, ay + 4, 12, 3, kTextMut);
  }

  // Air quality.
  String airBig = world_.airValid ? ("AQI " + String(world_.air.usAqi, 0)) : "--";
  char airEx[64] = "";
  if (world_.airValid) {
    snprintf(airEx, sizeof(airEx), "pm2.5 %.0f  pm10 %.0f  UV %.0f", world_.air.pm25, world_.air.pm10,
             world_.air.uvIndex);
  }
  drawWorldCard_(c1x, r1y, cw, chh, "AIR QUALITY", airBig, world_.air.category, airEx,
                 world_.airValid, world_.airMs, kGreen);

  const char *rl = kLinkAccent ? "REFRESH FEEDS" : "REFRESH (MOCK)";
  touchButton(g, kWorldRefresh.x, kWorldRefresh.y, kWorldRefresh.w, kWorldRefresh.h, rl, false);
}

// --- EVENTS ------------------------------------------------------------------

void ControlHubDashboard::drawEvents_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  if (eventCount_ == 0) {
    text(g, kW / 2, 260, "no events yet", fontL(), kTextMut, kCenter);
    return;
  }
  const int16_t rowH = 40;
  const uint8_t maxRows = 10;
  uint8_t rows = (eventCount_ < maxRows) ? eventCount_ : maxRows;
  for (uint8_t i = 0; i < rows; i++) {
    // Newest first: walk back from the most recent write.
    uint8_t idx = (uint8_t)((eventHead_ + kEventCap - 1 - i) % kEventCap);
    int16_t y = 92 + i * rowH;
    panel(g, 20, y, kW - 40, rowH - 8, 8, (i % 2) ? kBg : kSurface, 0, kLine);
    statusDot(g, 40, y + (rowH - 8) / 2, 5, i == 0 ? kAccent : kLine);
    text(g, 60, y + 6, fit(g, events_[idx], fontM(), kW - 220).c_str(), fontM(), kTextHi, kLeft);
    text(g, kW - 36, y + 8, ageStr(eventMs_[idx]).c_str(), fontS(), kTextMut, kRight);
  }
}

#else  // ---------------- display disabled: Serial-only no-op renderers --------

void ControlHubDashboard::begin() {}
void ControlHubDashboard::onSensor(const SensorReading &) {}
void ControlHubDashboard::onDevice(const ControlDevice &) {}
void ControlHubDashboard::onEvent(const String &) {}
void ControlHubDashboard::onWorldFeeds(const WorldFeeds &) {}
HubUiEvent ControlHubDashboard::tick() { return HubUiEvent(); }

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4

// ---------------------------------------------------------------------------
// Serial parity / diagnostics (defined in both builds).
// ---------------------------------------------------------------------------

const char *ControlHubDashboard::screenName() const {
  switch (screen_) {
    case HUB_DEVICES: return "DEVICES";
    case HUB_DETAIL: return "DETAIL";
    case HUB_SENSORS: return "SENSORS";
    case HUB_WORLD: return "WORLD";
    case HUB_EVENTS: return "EVENTS";
    default: return "?";
  }
}

bool ControlHubDashboard::selectScreenByName(const String &nameIn) {
  String n = nameIn;
  n.trim();
  n.toLowerCase();
  HubScreen t;
  if (n == "devices" || n == "device") t = HUB_DEVICES;
  else if (n == "detail") t = HUB_DETAIL;
  else if (n == "sensors" || n == "sensor") t = HUB_SENSORS;
  else if (n == "world") t = HUB_WORLD;
  else if (n == "events" || n == "event" || n == "log") t = HUB_EVENTS;
  else return false;
  screen_ = t;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  dirty_ = true;
#endif
  return true;
}

bool ControlHubDashboard::selectSensorByName(const String &nameIn) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  String n = nameIn;
  n.trim();
  if (n.length() == 0) return false;
  for (uint8_t i = 0; i < kMaxSensors; i++) {
    if (sensors_[i].used && sensors_[i].name.equalsIgnoreCase(n)) {
      pinnedSensor_ = (int8_t)i;
      screen_ = HUB_SENSORS;  // surface the pin so the effect is visible
      dirty_ = true;
      return true;
    }
  }
  return false;
#else
  // Headless: the dashboard keeps no sensor list, so there is nothing to pin.
  (void)nameIn;
  return false;
#endif
}

void ControlHubDashboard::printTouch(Print &out) const {
  out.print(F("[touch] screen="));
  out.print(screenName());
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  out.print(F(" raw="));
  out.print(touch_.rawX());
  out.print(',');
  out.print(touch_.rawY());
  out.print(F(" mapped="));
  out.print(touch_.x());
  out.print(',');
  out.print(touch_.y());
  out.print(F(" down="));
  out.print(touch_.down() ? 1 : 0);
  out.print(F(" taps="));
  out.println(touch_.count());
#else
  out.println(F(" raw=n/a mapped=n/a (headless build; rebuild -DUSE_DISPLAY=1 for live touch)"));
#endif
}
