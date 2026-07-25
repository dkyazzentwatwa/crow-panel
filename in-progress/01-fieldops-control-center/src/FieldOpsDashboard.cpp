#include "FieldOpsDashboard.h"

#include <CrowPanelShared.h>
#include <math.h>

// ===========================================================================
//  Data model + logic - compiled in EVERY build (headless included) so the
//  `selftest` command can drive the whole flow with no panel attached. The
//  Arduino_GFX drawing lives further down behind USE_DISPLAY.
// ===========================================================================

int8_t FieldOpsDashboard::findOrAddNode(const String &name) {
  for (uint8_t i = 0; i < kMaxNodes; i++) {
    if (nodes_[i].used && nodes_[i].name == name) return (int8_t)i;
  }
  for (uint8_t i = 0; i < kMaxNodes; i++) {
    if (!nodes_[i].used) {
      nodes_[i] = NodeState();
      nodes_[i].name = name;
      nodes_[i].used = true;
      nodeCount_++;
      return (int8_t)i;
    }
  }
  // Full: evict the stalest node.
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < kMaxNodes; i++) {
    if (nodes_[i].lastSeenMs < nodes_[oldest].lastSeenMs) oldest = i;
  }
  nodes_[oldest] = NodeState();
  nodes_[oldest].name = name;
  nodes_[oldest].used = true;
  return (int8_t)oldest;
}

bool FieldOpsDashboard::isStale(const NodeState &n) const {
  return (millis() - n.lastSeenMs) > kStaleMs;
}

uint8_t FieldOpsDashboard::visibleNodes(int8_t *out, uint8_t maxOut) const {
  int8_t idx[kMaxNodes];
  uint8_t c = 0;
  for (uint8_t i = 0; i < kMaxNodes; i++) {
    if (nodes_[i].used) idx[c++] = (int8_t)i;
  }
  // Insertion sort by lastSeen, newest first.
  for (uint8_t i = 1; i < c; i++) {
    int8_t v = idx[i];
    unsigned long lv = nodes_[v].lastSeenMs;
    int j = (int)i - 1;
    while (j >= 0 && nodes_[idx[j]].lastSeenMs < lv) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
  uint8_t n = (c < maxOut) ? c : maxOut;
  for (uint8_t i = 0; i < n; i++) out[i] = idx[i];
  return n;
}

int8_t FieldOpsDashboard::alertRingFromDisplay(uint8_t displayIdx) const {
  if (displayIdx >= alertCount_) return -1;
  // alertHead_ is the next slot to write, so the newest is head-1.
  return (int8_t)((alertHead_ + kMaxAlerts - 1 - displayIdx) % kMaxAlerts);
}

void FieldOpsDashboard::pushLog(const String &line) {
  strlcpy(log_[logHead_], line.c_str(), kLogLineLen);
  logTs_[logHead_] = millis();
  logHead_ = (logHead_ + 1) % kLogCap;
  if (logCount_ < kLogCap) logCount_++;
  logPushes_++;
}

uint8_t FieldOpsDashboard::severityOf(const String &alert) {
  // LOW_BATTERY and TEMP_WARNING are critical (red); MOTION_EVENT is a warning.
  if (alert.startsWith("LOW_BATTERY") || alert.startsWith("TEMP")) return 1;
  return 0;
}

void FieldOpsDashboard::nodeFromAlert(const String &alert, char *out, uint8_t outLen) {
  int s1 = alert.indexOf(' ');
  if (s1 < 0) {
    if (outLen) out[0] = '\0';
    return;
  }
  int s2 = alert.indexOf(' ', s1 + 1);
  String node = (s2 < 0) ? alert.substring(s1 + 1) : alert.substring(s1 + 1, s2);
  strlcpy(out, node.c_str(), outLen);
}

// --- Data in --------------------------------------------------------------

void FieldOpsDashboard::onPacket(const SensorPacket &packet) {
  int8_t idx = findOrAddNode(packet.nodeId);
  if (idx < 0) return;
  NodeState &n = nodes_[idx];
  n.lastSeenMs = millis();
  n.rssi = packet.rssi;
  if (!packet.presenceOnly) {
    n.hasTelemetry = true;
    n.tempC = packet.temperatureC;
    n.humidityPct = packet.humidityPct;
    n.batteryPct = packet.batteryPct;
    n.motion = packet.motion;
    n.tempHist[n.histHead] = packet.temperatureC;
    n.histHead = (n.histHead + 1) % kHist;
    if (n.histCount < kHist) n.histCount++;
    char line[kLogLineLen];
    snprintf(line, sizeof(line), "NODE %s  %.1fC  %d%%  %ddBm", n.name.c_str(),
             n.tempC, (int)(n.batteryPct + 0.5f), n.rssi);
    pushLog(line);
  } else {
    pushLog(String("PRESENCE ") + n.name + "  " + n.rssi + "dBm");
  }
  lastNode_ = idx;
  packetCount_++;
  dirty_ = true;
}

void FieldOpsDashboard::onAlert(const String &alert) {
  if (alert.length() == 0) return;
  // Dedupe against the newest UNacked alert so the repeating mock sweep does
  // not flood the stream; just refresh its timestamp. An acked alert of the
  // same text still re-raises a fresh (unacked) entry.
  if (alertCount_ > 0) {
    int8_t newest = alertRingFromDisplay(0);
    if (newest >= 0 && !alerts_[newest].acked && alert == alerts_[newest].text) {
      alerts_[newest].tsMs = millis();
      dirty_ = true;
      return;
    }
  }
  AlertItem &a = alerts_[alertHead_];
  a = AlertItem();
  strlcpy(a.text, alert.c_str(), sizeof(a.text));
  nodeFromAlert(alert, a.node, sizeof(a.node));
  a.severity = severityOf(alert);
  a.tsMs = millis();
  a.used = true;
  alertHead_ = (alertHead_ + 1) % kMaxAlerts;
  if (alertCount_ < kMaxAlerts) alertCount_++;
  pushLog(String("ALERT ") + alert);
  dirty_ = true;
}

void FieldOpsDashboard::onSummary(const String &summary) {
  summary_ = summary;
  dirty_ = true;
}

void FieldOpsDashboard::note(const String &line) {
  pushLog(line);
  dirty_ = true;
}

// --- Serial-parity actions ------------------------------------------------

bool FieldOpsDashboard::setScreen(FieldOpsScreen screen) {
  if (screen >= FIELDOPS_SCR_COUNT || screen == screen_) return false;
  screen_ = screen;
  dirty_ = true;
  return true;
}

bool FieldOpsDashboard::pinNodeByIndex(int8_t modelIdx) {
  if (modelIdx < 0 || modelIdx >= (int8_t)kMaxNodes || !nodes_[modelIdx].used) {
    return false;
  }
  pinnedNode_ = modelIdx;
  dirty_ = true;
  return true;
}

int8_t FieldOpsDashboard::pinNodeByName(const String &name) {
  for (uint8_t i = 0; i < kMaxNodes; i++) {
    if (nodes_[i].used && nodes_[i].name.equalsIgnoreCase(name)) {
      pinNodeByIndex((int8_t)i);
      return (int8_t)i;
    }
  }
  return -1;
}

bool FieldOpsDashboard::ackAlertByIndex(int8_t alertIdx) {
  if (alertIdx < 0 || alertIdx >= (int8_t)kMaxAlerts) return false;
  AlertItem &a = alerts_[alertIdx];
  if (!a.used || a.acked) return false;
  a.acked = true;
  dirty_ = true;
  return true;
}

int8_t FieldOpsDashboard::ackAlertByDisplay(uint8_t displayIdx) {
  int8_t ring = alertRingFromDisplay(displayIdx);
  if (ring >= 0 && ackAlertByIndex(ring)) return ring;
  return -1;
}

int8_t FieldOpsDashboard::ackNewestAlert() {
  for (uint8_t d = 0; d < alertCount_; d++) {
    int8_t ring = alertRingFromDisplay(d);
    if (ring >= 0 && !alerts_[ring].acked) {
      alerts_[ring].acked = true;
      dirty_ = true;
      return ring;
    }
  }
  return -1;
}

void FieldOpsDashboard::logPagePrev() {  // toward newer entries
  if (logPage_ > 0) {
    logPage_--;
    dirty_ = true;
  }
}

void FieldOpsDashboard::logPageNext() {  // toward older entries
  uint16_t last = (uint16_t)(logPageCount() - 1);
  if (logPage_ < last) {
    logPage_++;
    dirty_ = true;
  }
}

bool FieldOpsDashboard::setLogPage(uint16_t page) {
  uint16_t last = (uint16_t)(logPageCount() - 1);
  if (page > last) page = last;
  if (page == logPage_) return false;
  logPage_ = page;
  dirty_ = true;
  return true;
}

// --- Introspection --------------------------------------------------------

const char *FieldOpsDashboard::screenName() const {
  switch (screen_) {
    case FIELDOPS_SCR_ROSTER: return "ROSTER";
    case FIELDOPS_SCR_DETAIL: return "DETAIL";
    case FIELDOPS_SCR_ALERTS: return "ALERTS";
    case FIELDOPS_SCR_LOG: return "LOG";
    default: return "?";
  }
}

int8_t FieldOpsDashboard::activeNodeIndex() const {
  if (pinnedNode_ >= 0 && nodes_[pinnedNode_].used) return pinnedNode_;
  if (lastNode_ >= 0 && nodes_[lastNode_].used) return lastNode_;
  for (uint8_t i = 0; i < kMaxNodes; i++) {
    if (nodes_[i].used) return (int8_t)i;
  }
  return -1;
}

String FieldOpsDashboard::activeNodeName() const {
  int8_t a = activeNodeIndex();
  return a >= 0 ? nodes_[a].name : String("");
}

uint8_t FieldOpsDashboard::unackedAlertCount() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kMaxAlerts; i++) {
    if (alerts_[i].used && !alerts_[i].acked) n++;
  }
  return n;
}

uint16_t FieldOpsDashboard::logPageCount() const {
  uint16_t pages = (uint16_t)((logCount_ + kLogPerPage - 1) / kLogPerPage);
  return pages == 0 ? 1 : pages;
}

void FieldOpsDashboard::printTouch(Print &out) const {
  out.print(F("[touch] screen="));
  out.print(screenName());
  out.print(F(" taps="));
  out.print(touch_.count());
  out.print(F(" down="));
  out.print(touch_.down() ? 1 : 0);
  out.print(F(" raw="));
  out.print(touch_.rawX());
  out.print(F(","));
  out.print(touch_.rawY());
  out.print(F(" mapped="));
  out.print(touch_.x());
  out.print(F(","));
  out.println(touch_.y());
}

// --- Lifecycle / tick (drawing gated below) -------------------------------

void FieldOpsDashboard::begin() {
  pushLog("FieldOps UI booted");
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush: draw a whole frame, then flush once (see DisplayBringup) -
  // turns per-pixel cache syncs into one sync per frame.
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "FieldOps Control Center",
                              /*manualFlush=*/true) &&
           CrowDisplay::canvas() != nullptr;
#endif
  dirty_ = true;
}

bool FieldOpsDashboard::tick(FieldOpsUiEvent &event) {
  event = FieldOpsUiEvent();
  touch_.tick();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return false;
  bool haveEvent = handleTouch_(event);
  // Repaint on state change; otherwise refresh live fields (ages/uptime) ~1 Hz.
  if (dirty_ || (millis() - lastDrawMs_) >= 1000) {
    draw_();
    dirty_ = false;
    lastDrawMs_ = millis();
  }
  return haveEvent;
#else
  return false;
#endif
}

// ===========================================================================
//  Rendering + touch hit-testing - display builds only.
// ===========================================================================
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>

namespace {
using namespace Widgets;

// Layout (1024 x 600). Header + tab strip come from the shared chrome.
constexpr int16_t kW = 1024;
constexpr int16_t kContentTop = kChromeHeaderH;  // 72
constexpr int16_t kContentBot = kChromeTabY;     // 536

// Roster grid: 2 columns x 3 rows.
constexpr int16_t kCardMargin = 16;
constexpr int16_t kCardGap = 16;
constexpr int16_t kCardW = (kW - kCardMargin * 2 - kCardGap) / 2;  // 488
constexpr int16_t kCardH = 124;
constexpr int16_t kCardTop = 84;
constexpr int16_t kCardVGap = 16;

int16_t cardX(uint8_t slot) { return kCardMargin + (slot % 2) * (kCardW + kCardGap); }
int16_t cardY(uint8_t slot) { return kCardTop + (slot / 2) * (kCardH + kCardVGap); }

// Alert rows.
constexpr int16_t kAlertTop = 84;
constexpr int16_t kAlertH = 64;
constexpr int16_t kAlertGap = 8;
int16_t alertY(uint8_t row) { return kAlertTop + row * (kAlertH + kAlertGap); }

// Log paging buttons.
constexpr int16_t kPageBtnY = 480;
constexpr int16_t kPageBtnH = 48;
constexpr int16_t kPrevBtnX = 16;
constexpr int16_t kNextBtnX = kW - 16 - 200;
constexpr int16_t kPageBtnW = 200;

// Transport label for the header pill (resolved at compile time).
#if USE_ESPNOW
constexpr const char *kLink = "ESP-NOW";
#elif USE_LORA_DRIVER
constexpr const char *kLink = "SX1262";
#else
constexpr const char *kLink = "LoRa MOCK";
#endif

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

void ageText(char *buf, uint8_t len, unsigned long sinceMs) {
  unsigned long s = (millis() - sinceMs) / 1000;
  if (s < 100) snprintf(buf, len, "%lus", s);
  else snprintf(buf, len, "%lum", s / 60);
}
}  // namespace

// --- Touch dispatch -------------------------------------------------------

bool FieldOpsDashboard::handleTouch_(FieldOpsUiEvent &event) {
  if (!touch_.releasedEdge()) return false;
  const int16_t x = touch_.releaseX();
  const int16_t y = touch_.releaseY();

  // Bottom tab strip owns navigation on every screen.
  int8_t tab = tabHit(x, y, FIELDOPS_SCR_COUNT);
  if (tab >= 0) {
    setScreen((FieldOpsScreen)tab);
    return false;
  }

  switch (screen_) {
    case FIELDOPS_SCR_ROSTER: {
      int8_t slot = -1;
      for (uint8_t s = 0; s < kRosterCards; s++) {
        if (hitRect(x, y, cardX(s), cardY(s), kCardW, kCardH)) { slot = s; break; }
      }
      if (slot >= 0) {
        int8_t vis[kRosterCards];
        uint8_t vc = visibleNodes(vis, kRosterCards);
        if (slot < vc) {
          int8_t modelIdx = vis[slot];
          pinNodeByIndex(modelIdx);
          screen_ = FIELDOPS_SCR_DETAIL;
          dirty_ = true;
          event.type = FIELDOPS_EVT_PIN_NODE;
          event.index = modelIdx;
          strlcpy(event.label, nodes_[modelIdx].name.c_str(), sizeof(event.label));
          return true;
        }
      }
      break;
    }
    case FIELDOPS_SCR_ALERTS: {
      uint8_t rows = alertCount_ < kAlertRows ? alertCount_ : kAlertRows;
      for (uint8_t i = 0; i < rows; i++) {
        if (hitRect(x, y, kCardMargin, alertY(i), kW - kCardMargin * 2, kAlertH)) {
          int8_t ring = alertRingFromDisplay(i);
          if (ring >= 0 && !alerts_[ring].acked) {
            ackAlertByIndex(ring);
            event.type = FIELDOPS_EVT_ACK_ALERT;
            event.index = ring;
            strlcpy(event.label, alerts_[ring].text, sizeof(event.label));
            return true;
          }
          break;
        }
      }
      break;
    }
    case FIELDOPS_SCR_LOG: {
      if (hitRect(x, y, kPrevBtnX, kPageBtnY, kPageBtnW, kPageBtnH)) {
        logPagePrev();
      } else if (hitRect(x, y, kNextBtnX, kPageBtnY, kPageBtnW, kPageBtnH)) {
        logPageNext();
      }
      break;
    }
    case FIELDOPS_SCR_DETAIL:
    default:
      break;
  }
  return false;
}

// --- Frame ----------------------------------------------------------------

void FieldOpsDashboard::draw_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  drawHeader_();
  switch (screen_) {
    case FIELDOPS_SCR_ROSTER: drawRoster_(); break;
    case FIELDOPS_SCR_DETAIL: drawDetail_(); break;
    case FIELDOPS_SCR_ALERTS: drawAlerts_(); break;
    case FIELDOPS_SCR_LOG: drawLog_(); break;
    default: break;
  }
  drawTabs_();
  CrowDisplay::flush();
}

void FieldOpsDashboard::drawHeader_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const char *sub;
  switch (screen_) {
    case FIELDOPS_SCR_ROSTER: sub = "NODE ROSTER"; break;
    case FIELDOPS_SCR_DETAIL: sub = "NODE DETAIL"; break;
    case FIELDOPS_SCR_ALERTS: sub = "ALERT STREAM"; break;
    case FIELDOPS_SCR_LOG: sub = "EVENT LOG"; break;
    default: sub = ""; break;
  }
  headerBar(g, "FIELDOPS", sub, kLink, kAccent);

  // Unacked-alert badge, left of the transport pill.
  uint8_t unacked = unackedAlertCount();
  if (unacked > 0) {
    char b[16];
    snprintf(b, sizeof(b), "%u ALERT%s", unacked, unacked == 1 ? "" : "S");
    int16_t linkW = textWidth(g, kLink, fontS()) + 24;
    int16_t badgeW = textWidth(g, b, fontS()) + 24;
    pill(g, kW - 24 - linkW - 8 - badgeW, (kChromeHeaderH - 28) / 2, b, fontS(),
         kBg, kAmber);
  }
}

void FieldOpsDashboard::drawTabs_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  static const char *const kTabs[FIELDOPS_SCR_COUNT] = {"ROSTER", "DETAIL",
                                                        "ALERTS", "LOG"};
  tabBar(g, kTabs, FIELDOPS_SCR_COUNT, (uint8_t)screen_, kAccent);
}

// --- ROSTER ---------------------------------------------------------------

void FieldOpsDashboard::drawRoster_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  int8_t vis[kRosterCards];
  uint8_t vc = visibleNodes(vis, kRosterCards);
  for (uint8_t s = 0; s < kRosterCards; s++) {
    drawNodeCard_(s, s < vc ? vis[s] : -1);
  }
  // AI shift-summary strip along the bottom of the content area.
  text(g, kCardMargin, 506, "AI", fontS(), kAccent, kLeft);
  text(g, kCardMargin + 30, 506,
       fit(g, summary_, fontS(), kW - kCardMargin - 30 - 16).c_str(), fontS(),
       kTextMut, kLeft);
}

void FieldOpsDashboard::drawNodeCard_(uint8_t slot, int8_t modelIdx) {
  Arduino_GFX *g = CrowDisplay::canvas();
  int16_t x = cardX(slot);
  int16_t y = cardY(slot);

  if (modelIdx < 0) {
    panel(g, x, y, kCardW, kCardH, 12, kBg, 1, kLine);
    text(g, x + kCardW / 2, y + kCardH / 2 - 8, "EMPTY", fontS(), kLine, kCenter);
    return;
  }

  const NodeState &n = nodes_[modelIdx];
  bool active = (modelIdx == activeNodeIndex());
  bool pinned = (modelIdx == pinnedNode_);
  bool stale = isStale(n);

  panel(g, x, y, kCardW, kCardH, 12, active ? kSurfaceHi : kSurface,
        (active || pinned) ? 2 : 1, pinned ? kAccent : kLine);

  // Status dot.
  uint16_t dot;
  if (stale) dot = kLine;
  else if (!n.hasTelemetry) dot = kAccent;
  else if (n.batteryPct < 35.0f || n.tempC > 35.0f) dot = kRed;
  else if (n.batteryPct < 55.0f || n.tempC > 27.0f) dot = kAmber;
  else dot = kGreen;
  statusDot(g, x + 26, y + 28, 7, dot);

  // Name + age.
  text(g, x + 46, y + 14, fit(g, n.name, fontL(), 250).c_str(), fontL(),
       stale ? kTextMut : kTextHi, kLeft);
  char age[10];
  ageText(age, sizeof(age), n.lastSeenMs);
  text(g, x + kCardW - 16, y + 16, age, fontS(), kTextMut, kRight);

  if (pinned) {
    text(g, x + kCardW - 16, y + kCardH - 26, "PINNED", fontS(), kAccent, kRight);
  }

  if (n.hasTelemetry) {
    char t[10];
    snprintf(t, sizeof(t), "%.1fC", n.tempC);
    text(g, x + 24, y + 44, t, fontXL(), stale ? kTextMut : tempColor(n.tempC),
         kLeft);
    signalBars(g, x + 26, y + kCardH - 14, stale ? 0 : rssiLevel(n.rssi),
               stale ? kLine : kAccent);

    // Battery + humidity on the right half.
    text(g, x + 250, y + 46, "BATTERY", fontS(), kTextMut, kLeft);
    hBar(g, x + 250, y + 66, 160, 12, n.batteryPct / 100.0f,
         batteryColor(n.batteryPct));
    char b[8];
    snprintf(b, sizeof(b), "%d%%", (int)(n.batteryPct + 0.5f));
    text(g, x + 420, y + 62, b, fontS(), kTextHi, kLeft);
    char h[16];
    snprintf(h, sizeof(h), "HUM %d%%", (int)(n.humidityPct + 0.5f));
    text(g, x + 250, y + 96, h, fontS(), kTextMut, kLeft);
    if (n.motion) {
      pill(g, x + 360, y + 90, "MOTION", fontS(), kBg, kAmber);
    }
  } else {
    text(g, x + 24, y + 48, "PRESENCE", fontL(), kAccent, kLeft);
    text(g, x + 24, y + 84, "chat node - no telemetry", fontS(), kTextMut, kLeft);
    signalBars(g, x + kCardW - 60, y + kCardH - 14, stale ? 0 : rssiLevel(n.rssi),
               stale ? kLine : kAccent);
  }
}

// --- DETAIL ---------------------------------------------------------------

void FieldOpsDashboard::drawDetail_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  int8_t a = activeNodeIndex();
  if (a < 0) {
    text(g, kW / 2, 280, "waiting for nodes...", fontM(), kTextMut, kCenter);
    return;
  }
  const NodeState &n = nodes_[a];

  // Title row.
  bool pinnedActive = (pinnedNode_ >= 0 && pinnedNode_ == a);
  text(g, 24, 84, fit(g, n.name, fontL(), 520).c_str(), fontL(), kTextHi, kLeft);
  const char *tag = pinnedActive ? "PINNED" : "AUTO-FOLLOW";
  int16_t tagW = textWidth(g, tag, fontS()) + 24;
  pill(g, kW - 24 - tagW, 82, tag, fontS(), kBg, pinnedActive ? kAccent : kLine);

  if (!n.hasTelemetry) {
    text(g, kW / 2, 230, "PRESENCE NODE", fontL(), kAccent, kCenter);
    text(g, kW / 2, 268, "chat heartbeat - no telemetry to gauge", fontS(),
         kTextMut, kCenter);
    signalBars(g, kW / 2 - 22, 330, isStale(n) ? 0 : rssiLevel(n.rssi), kAccent);
    return;
  }

  // Three ring gauges: battery, temperature, humidity.
  const int16_t cy = 220, rOut = 88, rIn = 66;
  const int16_t cxs[3] = {200, 512, 824};

  arcGauge(g, cxs[0], cy, rOut, rIn, n.batteryPct / 100.0f,
           batteryColor(n.batteryPct));
  char b[8];
  snprintf(b, sizeof(b), "%d%%", (int)(n.batteryPct + 0.5f));
  text(g, cxs[0], cy - 24, b, fontXL(), kTextHi, kCenter);
  text(g, cxs[0], cy + 26, "BATTERY", fontS(), kTextMut, kCenter);

  arcGauge(g, cxs[1], cy, rOut, rIn, n.tempC / 50.0f, tempColor(n.tempC));
  char t[10];
  snprintf(t, sizeof(t), "%.1f", n.tempC);
  text(g, cxs[1], cy - 24, t, fontXL(), kTextHi, kCenter);
  text(g, cxs[1], cy + 26, "TEMPERATURE C", fontS(), kTextMut, kCenter);

  arcGauge(g, cxs[2], cy, rOut, rIn, n.humidityPct / 100.0f, kAccent);
  char h[8];
  snprintf(h, sizeof(h), "%d%%", (int)(n.humidityPct + 0.5f));
  text(g, cxs[2], cy - 24, h, fontXL(), kTextHi, kCenter);
  text(g, cxs[2], cy + 26, "HUMIDITY", fontS(), kTextMut, kCenter);

  // Temperature-trend sparkline panel.
  const int16_t spX = 16, spY = 330, spW = kW - 32, spH = 140;
  panel(g, spX, spY, spW, spH, 12, kSurface, 1, kLine);
  text(g, spX + 16, spY + 12, "TEMPERATURE TREND", fontS(), kTextMut, kLeft);
  if (n.histCount >= 2) {
    uint16_t tail = (n.histHead + kHist - n.histCount) % kHist;
    sparkline(g, spX + 16, spY + 40, spW - 32, spH - 56, n.tempHist, n.histCount,
              tail, 0.0f, 0.0f, kAccent, kSurfaceHi);
  } else {
    text(g, spX + 16, spY + spH / 2, "collecting samples...", fontS(), kTextMut,
         kLeft);
  }

  // Stats strip: signal + motion + last-seen.
  const int16_t sy = 486;
  text(g, 24, sy, "SIGNAL", fontS(), kTextMut, kLeft);
  signalBars(g, 110, sy + 18, rssiLevel(n.rssi), kAccent);
  char r[14];
  snprintf(r, sizeof(r), "%d dBm", n.rssi);
  text(g, 190, sy + 2, r, fontS(), kTextHi, kLeft);

  if (n.motion) pill(g, 330, sy - 4, "MOTION", fontS(), kBg, kAmber);
  else pill(g, 330, sy - 4, "IDLE", fontS(), kTextMut, kSurfaceHi);

  char age[16];
  snprintf(age, sizeof(age), "last seen %lus ago",
           (unsigned long)((millis() - n.lastSeenMs) / 1000));
  text(g, kW - 24, sy + 2, age, fontS(), kTextMut, kRight);
}

// --- ALERTS ---------------------------------------------------------------

void FieldOpsDashboard::drawAlerts_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (alertCount_ == 0) {
    text(g, kW / 2, 260, "NO ALERTS", fontL(), kGreen, kCenter);
    text(g, kW / 2, 296, "systems nominal - mock sweep is within range", fontS(),
         kTextMut, kCenter);
    return;
  }

  uint8_t rows = alertCount_ < kAlertRows ? alertCount_ : kAlertRows;
  for (uint8_t i = 0; i < rows; i++) {
    int8_t ring = alertRingFromDisplay(i);
    if (ring < 0) continue;
    const AlertItem &a = alerts_[ring];
    int16_t y = alertY(i);
    bool crit = a.severity == 1;
    uint16_t sev = crit ? kRed : kAmber;
    uint16_t border = a.acked ? kLine : sev;
    panel(g, kCardMargin, y, kW - kCardMargin * 2, kAlertH, 10, kSurface, 2,
          border);

    // Severity chip.
    pill(g, kCardMargin + 14, y + 18, crit ? "CRIT" : "WARN", fontS(), kBg,
         a.acked ? kLine : sev);

    // Alert text + node.
    uint16_t ink = a.acked ? kTextMut : kTextHi;
    int16_t textX = kCardMargin + 100;
    text(g, textX, y + 12, fit(g, String(a.text), fontS(), 640).c_str(), fontS(),
         ink, kLeft);
    char meta[28];
    snprintf(meta, sizeof(meta), "node %s  %lus ago", a.node,
             (unsigned long)((millis() - a.tsMs) / 1000));
    text(g, textX, y + 38, meta, fontS(), kTextMut, kLeft);

    // Ack state on the right.
    if (a.acked) {
      pill(g, kW - kCardMargin - 14 - 70, y + 18, "ACKED", fontS(), kTextMut,
           kSurfaceHi);
    } else {
      text(g, kW - kCardMargin - 20, y + 24, "TAP TO ACK", fontS(), kAccent,
           kRight);
    }
  }
}

// --- LOG ------------------------------------------------------------------

void FieldOpsDashboard::drawLog_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, 16, kCardTop, kW - 32, 384, 12, kSurface, 1, kLine);

  if (logCount_ == 0) {
    text(g, kW / 2, 260, "log empty", fontS(), kTextMut, kCenter);
  } else {
    uint16_t start = logPage_ * kLogPerPage;  // 0 = newest
    for (uint8_t i = 0; i < kLogPerPage; i++) {
      uint16_t k = start + i;  // 0 = newest
      if (k >= logCount_) break;
      uint16_t ring = (uint16_t)((logHead_ + kLogCap - 1 - k) % kLogCap);
      int16_t ly = kCardTop + 16 + i * 45;
      char ts[14];
      snprintf(ts, sizeof(ts), "t+%lu.%lus", (unsigned long)(logTs_[ring] / 1000),
               (unsigned long)((logTs_[ring] % 1000) / 100));
      text(g, 32, ly, ts, fontS(), kAccent, kLeft);
      text(g, 150, ly, fit(g, String(log_[ring]), fontS(), kW - 150 - 40).c_str(),
           fontS(), kTextHi, kLeft);
      if (i < kLogPerPage - 1 && k + 1 < logCount_) {
        g->drawFastHLine(32, ly + 30, kW - 64, kLine);
      }
    }
  }

  // Paging controls.
  bool canNewer = logPage_ > 0;
  bool canOlder = logPage_ + 1 < logPageCount();
  touchButton(g, kPrevBtnX, kPageBtnY, kPageBtnW, kPageBtnH, "< NEWER", canNewer);
  touchButton(g, kNextBtnX, kPageBtnY, kPageBtnW, kPageBtnH, "OLDER >", canOlder);
  char pageBuf[24];
  snprintf(pageBuf, sizeof(pageBuf), "PAGE %u / %u", (unsigned)(logPage_ + 1),
           (unsigned)logPageCount());
  text(g, kW / 2, kPageBtnY + 16, pageBuf, fontS(), kTextMut, kCenter);
  char cntBuf[24];
  snprintf(cntBuf, sizeof(cntBuf), "%u events", (unsigned)logCount_);
  text(g, kW / 2, kPageBtnY + 34, cntBuf, fontS(), kTextMut, kCenter);
}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
