#include "FieldOpsDashboard.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <math.h>

namespace {
using namespace Widgets;

// --- Layout (1024 x 600 landscape) ---
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

// Transport label for the header pill, resolved at compile time.
#if USE_ESPNOW
const char *kLinkLabel = "ESP-NOW";
constexpr bool kLinkAccent = true;
#elif USE_LORA_DRIVER
const char *kLinkLabel = "SX1262";
constexpr bool kLinkAccent = true;
#else
const char *kLinkLabel = "LoRa MOCK";
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

int8_t FieldOpsDashboard::activeNode() const {
  if (pinnedNode_ >= 0 && nodes_[pinnedNode_].used) return pinnedNode_;
  if (lastNode_ >= 0 && nodes_[lastNode_].used) return lastNode_;
  for (uint8_t i = 0; i < kMaxNodes; i++) {
    if (nodes_[i].used) return (int8_t)i;
  }
  return -1;
}

bool FieldOpsDashboard::isStale(const NodeState &n) const {
  return (millis() - n.lastSeenMs) > kStaleMs;
}

uint8_t FieldOpsDashboard::visibleNodes(int8_t *out) const {
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
  uint8_t n = (c < kVisibleCards) ? c : kVisibleCards;
  for (uint8_t i = 0; i < n; i++) out[i] = idx[i];
  return n;
}

void FieldOpsDashboard::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "FieldOps Control Center") &&
           (CrowDisplay::canvas() != nullptr);
  if (!ready_) return;
  paintChrome();
  repaint();
}

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
    alert_ = "";  // telemetry packet resets the banner; onAlert may re-set it
  }
  lastNode_ = idx;
  packetCount_++;
  dirty_ = true;
}

void FieldOpsDashboard::onAlert(const String &alert) {
  alert_ = alert;
  dirty_ = true;
}

void FieldOpsDashboard::onSummary(const String &summary) {
  summary_ = summary;
  dirty_ = true;
}

void FieldOpsDashboard::tick() {
  if (!ready_) return;

  int16_t tx, ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  if (touched && !wasTouched_) {
    int8_t vis[kVisibleCards];
    uint8_t vc = visibleNodes(vis);
    int8_t hit = -1;
    for (uint8_t s = 0; s < vc; s++) {
      int16_t y = cardYPos(s);
      if (tx >= kCardX && tx <= kCardX + kCardW && ty >= y && ty <= y + kCardH) {
        hit = vis[s];
        break;
      }
    }
    pinnedNode_ = (hit >= 0) ? hit : -1;
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
    drawRoster();  // node ages tick down
    drawFooter();
  }
}

void FieldOpsDashboard::paintChrome() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);
  towerIcon(g, 18, 20, kAccent);
  text(g, 58, 14, "FIELDOPS", fontL(), kTextHi, kLeft);
  text(g, 58, 44, "CONTROL CENTER", fontS(), kTextMut, kLeft);
  text(g, kCardX + 4, 86, "NODES", fontS(), kTextMut, kLeft);
}

void FieldOpsDashboard::repaint() {
  if (CrowDisplay::canvas() == nullptr) return;
  drawHeaderStatus();
  drawRoster();
  drawBanner();
  drawGauges();
  drawStats();
  drawSparkline();
  drawFooter();
}

void FieldOpsDashboard::drawHeaderStatus() {
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

void FieldOpsDashboard::drawRoster() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  // Clear the roster column (below the "NODES" label) so vanished nodes go away.
  g->fillRect(0, 104, 360, kFooterY - 104, kBg);

  int8_t vis[kVisibleCards];
  uint8_t vc = visibleNodes(vis);
  for (uint8_t s = 0; s < kVisibleCards; s++) {
    drawNodeCard(s, s < vc ? vis[s] : -1);
  }
}

void FieldOpsDashboard::drawNodeCard(uint8_t slot, int8_t nodeIdx) {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || nodeIdx < 0) return;

  const NodeState &n = nodes_[nodeIdx];
  int16_t y = cardYPos(slot);
  bool active = (nodeIdx == activeNode());
  bool stale = isStale(n);

  uint16_t fill = active ? kSurfaceHi : kSurface;
  panel(g, kCardX, y, kCardW, kCardH, 10, fill, active ? 2 : 0, kAccent);

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

  // Age (top-right).
  char age[10];
  snprintf(age, sizeof(age), "%lus", (unsigned long)((millis() - n.lastSeenMs) / 1000));
  text(g, kCardX + kCardW - 12, y + 10, age, fontS(), kTextMut, kRight);

  // Row 2: telemetry or presence.
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

void FieldOpsDashboard::drawBanner() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;

  uint16_t fill = kGreen;
  const char *label = "SYSTEMS NOMINAL";
  String detail = summary_;
  if (alert_.length() > 0) {
    fill = (alert_.startsWith("LOW_BATTERY") || alert_.startsWith("TEMP")) ? kRed : kAmber;
    label = "ALERT";
    detail = alert_;
  }

  panel(g, kRX, kBannerY, kRW, kBannerH, 8, fill);
  text(g, kRX + 16, kBannerY + 13, label, fontS(), kBg, kLeft);
  int16_t detailX = kRX + 16 + textWidth(g, label, fontS()) + 18;
  text(g, detailX, kBannerY + 13, fit(g, detail, fontS(), kRX + kRW - 12 - detailX).c_str(),
       fontS(), kBg, kLeft);
}

void FieldOpsDashboard::drawGauges() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(kRX, 140, kRW, 200, kBg);

  int8_t a = activeNode();
  int16_t cx = kRX + kRW / 2;
  if (a < 0) {
    text(g, cx, 220, "waiting for nodes...", fontM(), kTextMut, kCenter);
    return;
  }

  const NodeState &n = nodes_[a];
  if (!n.hasTelemetry) {
    text(g, cx, 190, "PRESENCE", fontL(), kAccent, kCenter);
    text(g, cx, 224, fit(g, n.name, fontM(), kRW - 40).c_str(), fontM(), kTextHi, kCenter);
    text(g, cx, 256, "chat node - no telemetry", fontS(), kTextMut, kCenter);
    return;
  }

  int16_t cxA = kRX + 150;
  int16_t cxB = kRX + kRW - 150;

  arcGauge(g, cxA, kGaugeCy, kGaugeROut, kGaugeRIn, n.batteryPct / 100.0f, batteryColor(n.batteryPct));
  char b[8];
  snprintf(b, sizeof(b), "%d%%", (int)(n.batteryPct + 0.5f));
  text(g, cxA, kGaugeCy - 26, b, fontXL(), kTextHi, kCenter);
  text(g, cxA, kGaugeCy + 24, "BATTERY", fontS(), kTextMut, kCenter);

  arcGauge(g, cxB, kGaugeCy, kGaugeROut, kGaugeRIn, n.tempC / 50.0f, tempColor(n.tempC));
  char t[10];
  snprintf(t, sizeof(t), "%.1f", n.tempC);
  text(g, cxB, kGaugeCy - 26, t, fontXL(), kTextHi, kCenter);
  text(g, cxB, kGaugeCy + 24, "TEMPERATURE C", fontS(), kTextMut, kCenter);
}

void FieldOpsDashboard::drawStats() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(kRX, kStatsY, kRW, 52, kBg);

  int8_t a = activeNode();
  if (a < 0) return;
  const NodeState &n = nodes_[a];

  if (n.hasTelemetry) {
    text(g, kRX, kStatsY, "HUMIDITY", fontS(), kTextMut, kLeft);
    hBar(g, kRX, kStatsY + 24, 200, 12, n.humidityPct / 100.0f, kAccent);
    char h[10];
    snprintf(h, sizeof(h), "%d%%", (int)(n.humidityPct + 0.5f));
    text(g, kRX + 212, kStatsY + 22, h, fontS(), kTextHi, kLeft);
  }

  int16_t sx = kRX + 300;
  text(g, sx, kStatsY, "SIGNAL", fontS(), kTextMut, kLeft);
  signalBars(g, sx, kStatsY + 40, rssiLevel(n.rssi), kAccent);
  char r[14];
  snprintf(r, sizeof(r), "%d dBm", n.rssi);
  text(g, sx + 60, kStatsY + 22, r, fontS(), kTextHi, kLeft);

  int16_t mx = kRX + kRW - 130;
  if (n.hasTelemetry && n.motion) {
    pill(g, mx, kStatsY + 16, "MOTION", fontS(), kBg, kAmber);
  } else if (n.hasTelemetry) {
    pill(g, mx, kStatsY + 16, "IDLE", fontS(), kTextMut, kSurfaceHi);
  }
}

void FieldOpsDashboard::drawSparkline() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  panel(g, kRX, kSparkY, kRW, kSparkH, 10, kSurface);
  text(g, kRX + 14, kSparkY + 12, "TEMPERATURE TREND", fontS(), kTextMut, kLeft);

  int8_t a = activeNode();
  if (a >= 0 && nodes_[a].hasTelemetry && nodes_[a].histCount >= 2) {
    const NodeState &n = nodes_[a];
    uint16_t tail = (n.histHead + kHist - n.histCount) % kHist;
    sparkline(g, kRX + 14, kSparkY + 36, kRW - 28, kSparkH - 50,
              n.tempHist, n.histCount, tail, 0.0f, 0.0f, kAccent, kSurfaceHi);
  } else {
    text(g, kRX + 14, kSparkY + 48, "no telemetry", fontS(), kTextMut, kLeft);
  }
}

void FieldOpsDashboard::drawFooter() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  g->fillRect(0, kFooterY, kScreenW, 2, kLine);

  statusDot(g, 24, kFooterY + 30, 5, kAccent);
  String ev = alert_.length() > 0 ? alert_ : summary_;
  text(g, 40, kFooterY + 22, fit(g, ev, fontS(), 560).c_str(), fontS(), kTextHi, kLeft);

  char buf[48];
  snprintf(buf, sizeof(buf), "NODES %u   PACKETS %lu   UP %lus",
           (unsigned)nodeCount_, (unsigned long)packetCount_,
           (unsigned long)(millis() / 1000));
  text(g, kScreenW - 16, kFooterY + 22, buf, fontS(), kTextMut, kRight);
}

#else  // display disabled: no-op so the sketch still runs Serial-only

void FieldOpsDashboard::begin() {}
void FieldOpsDashboard::onPacket(const SensorPacket &) {}
void FieldOpsDashboard::onAlert(const String &) {}
void FieldOpsDashboard::onSummary(const String &) {}
void FieldOpsDashboard::tick() {}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
