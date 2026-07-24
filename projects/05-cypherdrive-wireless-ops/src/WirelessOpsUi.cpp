#include "WirelessOpsUi.h"

#include <CrowPanelShared.h>

// ---------------------------------------------------------------------------
// Plain state + serial-parity navigation. These compile in BOTH the display and
// the headless build so `selftest`, `screen`, `net`, `page`, and `touch` behave
// identically with no panel attached.
// ---------------------------------------------------------------------------

void WirelessOpsUi::setWifi(const WifiNetworkRecord *rows, uint8_t count) {
  wifiCount_ = count < kMaxWifi ? count : kMaxWifi;
  for (uint8_t i = 0; i < wifiCount_; ++i) wifi_[i] = rows[i];
  if (wifiCount_ == 0) {
    wifiSel_ = 0;
    detail_ = false;
  } else if (wifiSel_ >= wifiCount_) {
    wifiSel_ = wifiCount_ - 1;
  }
  markDirty_();
}

void WirelessOpsUi::setBle(const BleAdvertisementRecord *rows, uint8_t count) {
  bleCount_ = count < kMaxBle ? count : kMaxBle;
  for (uint8_t i = 0; i < bleCount_; ++i) ble_[i] = rows[i];
  markDirty_();
}

void WirelessOpsUi::setLog(const ScanLog *log) {
  log_ = log;
  clampLogPage_();
  markDirty_();
}

void WirelessOpsUi::setQr(const String &url, bool persisted, const String &defaultUrl) {
  qrUrl_ = url;
  qrPersisted_ = persisted;
  qrDefault_ = defaultUrl;
  markDirty_();
}

void WirelessOpsUi::setStatus(const String &status) {
  status_ = status;
  markDirty_();
}

const char *WirelessOpsUi::screenName() const {
  switch (screen_) {
    case WSCR_WIFI: return detail_ ? "wifi-detail" : "wifi";
    case WSCR_BLE: return "ble";
    case WSCR_LOG: return "log";
    case WSCR_QR: return "qr";
    default: return "wifi";
  }
}

uint8_t WirelessOpsUi::logPageCount() const {
  uint8_t entries = log_ ? log_->count() : 0;
  if (entries == 0) return 1;
  return (uint8_t)((entries + kLogRowsPerPage - 1) / kLogRowsPerPage);
}

void WirelessOpsUi::clampLogPage_() {
  uint8_t pages = logPageCount();
  if (logPage_ >= pages) logPage_ = pages - 1;
}

bool WirelessOpsUi::showScreen(const String &name) {
  String n = name;
  n.trim();
  n.toLowerCase();
  WirelessScreen next = screen_;
  if (n == "wifi" || n == "wi-fi") next = WSCR_WIFI;
  else if (n == "ble" || n == "bt") next = WSCR_BLE;
  else if (n == "log" || n == "logs") next = WSCR_LOG;
  else if (n == "qr") next = WSCR_QR;
  else return false;
  screen_ = next;
  detail_ = false;
  markDirty_();
  return true;
}

bool WirelessOpsUi::selectNetwork(uint8_t index) {
  if (index >= wifiCount_) return false;
  wifiSel_ = index;
  screen_ = WSCR_WIFI;
  detail_ = true;
  markDirty_();
  return true;
}

void WirelessOpsUi::closeDetail() {
  detail_ = false;
  markDirty_();
}

void WirelessOpsUi::pageLog(int8_t dir) {
  uint8_t pages = logPageCount();
  if (dir < 0) {
    logPage_ = (logPage_ == 0) ? (uint8_t)(pages - 1) : (uint8_t)(logPage_ - 1);
  } else if (dir > 0) {
    logPage_ = (uint8_t)((logPage_ + 1) % pages);
  }
  clampLogPage_();
  markDirty_();
}

void WirelessOpsUi::setLogPage(uint8_t page) {
  logPage_ = page;
  clampLogPage_();
  markDirty_();
}

void WirelessOpsUi::printTouch(Print &out) const {
  out.print(F("[touch] raw="));
  out.print(touch_.rawX());
  out.print(F(","));
  out.print(touch_.rawY());
  out.print(F(" mapped="));
  out.print(touch_.x());
  out.print(F(","));
  out.print(touch_.y());
  out.print(F(" taps="));
  out.print(touch_.count());
  out.print(F(" down="));
  out.print(touch_.down() ? 1 : 0);
  out.print(F(" screen="));
  out.print(screenName());
  out.print(F(" wifi_sel="));
  out.print(wifiSel_);
  out.print(F(" log_page="));
  out.print(logPage_ + 1);
  out.print(F("/"));
  out.println(logPageCount());
}

// ===========================================================================
// Display build: rendering + touch. Everything below the guard is P4-only.
// ===========================================================================
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>

using namespace Widgets;

namespace {

constexpr int16_t kW = kChromeW;             // 1024
constexpr int16_t kTabY = kChromeTabY;       // 536
constexpr int16_t kBodyX = 24;
constexpr int16_t kBodyW = kW - 2 * kBodyX;  // 976

constexpr int16_t kActionY = 84;
constexpr int16_t kActionH = 40;

constexpr int16_t kGridY = 146;
constexpr int16_t kCardGap = 16;
constexpr int16_t kCardW = (kBodyW - kCardGap) / 2;  // 480
constexpr int16_t kCardH = 84;
constexpr int16_t kCardVGap = 12;

struct Rect {
  int16_t x, y, w, h;
};
bool inRect(const Rect &r, int16_t x, int16_t y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Shared action-strip controls (identical rects for draw + hit-test).
const Rect kScanBtn = {kW - kBodyX - 160, kActionY, 160, kActionH};
const Rect kBackBtn = {kBodyX, kActionY, 160, kActionH};
const Rect kNextBtn = {kW - kBodyX - 120, kActionY, 120, kActionH};
const Rect kPrevBtn = {kW - kBodyX - 120 - 12 - 120, kActionY, 120, kActionH};

Rect wifiCardRect(uint8_t i) {
  int16_t col = i % 2, row = i / 2;
  return {int16_t(kBodyX + col * (kCardW + kCardGap)),
          int16_t(kGridY + row * (kCardH + kCardVGap)), kCardW, kCardH};
}

uint8_t rssiLevel(int32_t rssi) {
  if (rssi >= -52) return 4;
  if (rssi >= -62) return 3;
  if (rssi >= -72) return 2;
  if (rssi >= -82) return 1;
  return 0;
}
uint16_t rssiColor(uint8_t level) {
  if (level >= 3) return kGreen;
  if (level == 2) return kAmber;
  return kRed;
}
const char *bandName(uint8_t channel) { return channel <= 14 ? "2.4 GHz" : "5 GHz"; }

uint16_t securityColor(const String &auth) {
  String a = auth;
  a.toLowerCase();
  if (a.indexOf("wpa3") >= 0 || a.indexOf("wpa2") >= 0) return kGreen;
  if (a == "open" || a.indexOf("wep") >= 0) return kRed;
  return kAmber;  // plain wpa / unknown
}

uint16_t logTypeColor(ScanLog::EntryType type) {
  switch (type) {
    case ScanLog::kWifi: return kAccent;
    case ScanLog::kBle: return rgb(0x8B, 0x5C, 0xF6);  // violet
    case ScanLog::kQr: return kAmber;
    default: return kTextMut;
  }
}

String fitText(Arduino_GFX *g, String s, int16_t maxW, const GFXfont *font) {
  s.trim();
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  while (s.length() > 1 && textWidth(g, (s + "...").c_str(), font) > maxW) {
    s.remove(s.length() - 1);
  }
  return s + "...";
}

// Wrap `s` into up to maxLines lines within width w, truncating the last line.
void drawWrapped(Arduino_GFX *g, String s, int16_t x, int16_t y, int16_t w, int16_t lineH,
                 uint8_t maxLines, const GFXfont *font, uint16_t color) {
  s.trim();
  if (s.length() == 0) s = "-";
  for (uint8_t line = 0; line < maxLines && s.length() > 0; ++line) {
    String chunk = s;
    while (chunk.length() > 0 && textWidth(g, chunk.c_str(), font) > w) {
      int split = chunk.lastIndexOf(' ');
      if (split <= 0) {
        chunk.remove(chunk.length() - 1);
      } else {
        chunk = chunk.substring(0, split);
      }
    }
    if (chunk.length() == 0) chunk = s.substring(0, 1);
    bool last = (line + 1 == maxLines) && (chunk.length() < s.length());
    if (last) chunk = fitText(g, s, w, font);
    text(g, x, y + line * lineH, chunk.c_str(), font, color, kLeft);
    s.remove(0, chunk.length());
    s.trim();
  }
}

void fieldBlock(Arduino_GFX *g, int16_t x, int16_t y, const char *label, const String &value,
                uint16_t valueColor = kTextHi) {
  text(g, x, y, label, fontS(), kTextMut, kLeft);
  text(g, x, y + 20, value.c_str(), fontL(), valueColor, kLeft);
}

// Decorative QR-style placeholder. Draws finder patterns + a deterministic
// sparse module field so it reads as "a QR", then a clear PENDING band so no
// one mistakes it for a scannable code. The URL beside it is the real handoff.
uint32_t fnv1a(const String &s) {
  uint32_t h = 2166136261u;
  for (uint16_t i = 0; i < s.length(); ++i) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

void drawFinder(Arduino_GFX *g, int16_t x, int16_t y, int16_t m, uint16_t fg, uint16_t bg) {
  g->fillRect(x, y, 7 * m, 7 * m, fg);
  g->fillRect(x + m, y + m, 5 * m, 5 * m, bg);
  g->fillRect(x + 2 * m, y + 2 * m, 3 * m, 3 * m, fg);
}

void drawQrPlaceholder(Arduino_GFX *g, int16_t x, int16_t y, int16_t size, const String &url) {
  const int16_t N = 21;            // version-1 module grid
  const int16_t quiet = 2;
  const int16_t total = N + 2 * quiet;
  const int16_t m = size / total;
  const int16_t grid = m * total;
  const uint16_t fg = kTextHi;
  const uint16_t bg = rgb(0xF4, 0xF7, 0xFB);
  g->fillRoundRect(x, y, grid, grid, 10, bg);
  const int16_t ox = x + quiet * m, oy = y + quiet * m;
  uint32_t h = fnv1a(url);
  for (int16_t r = 0; r < N; ++r) {
    for (int16_t c = 0; c < N; ++c) {
      bool finder = (r < 8 && c < 8) || (r < 8 && c >= N - 8) || (r >= N - 8 && c < 8);
      if (finder) continue;
      uint32_t bit = (h ^ ((uint32_t)(r * 31 + c) * 2654435761u));
      if ((bit & 5u) == 0u) g->fillRect(ox + c * m, oy + r * m, m, m, fg);
    }
  }
  drawFinder(g, ox, oy, m, fg, bg);
  drawFinder(g, ox + (N - 7) * m, oy, m, fg, bg);
  drawFinder(g, ox, oy + (N - 7) * m, m, fg, bg);
  // PENDING band so the placeholder can never be mistaken for a real code.
  const int16_t bandH = 40;
  const int16_t bandY = y + (grid - bandH) / 2;
  g->fillRect(x, bandY, grid, bandH, kBg);
  text(g, x + grid / 2, bandY + 12, "QR RENDER PENDING", fontS(), kAmber, kCenter);
}

}  // namespace

bool WirelessOpsUi::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "CYPHERDRIVE WIRELESS OPS",
                              /*manualFlush=*/true) &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
  return ready_;
}

void WirelessOpsUi::markDirty_() { dirty_ = true; }

WirelessAction WirelessOpsUi::tick() {
  touch_.tick();
  WirelessAction act = WACT_NONE;
  if (touch_.releasedEdge()) {
    act = handleRelease_(touch_.releaseX(), touch_.releaseY());
    dirty_ = true;  // clear press feedback / refresh selection
  }
  if (ready_ && dirty_) {
    draw_();
    dirty_ = false;
  }
  return act;
}

WirelessAction WirelessOpsUi::handleRelease_(int16_t x, int16_t y) {
  // Bottom tab strip wins everywhere.
  int8_t tab = tabHit(x, y, WSCR_COUNT);
  if (tab >= 0) {
    screen_ = (WirelessScreen)tab;
    detail_ = false;
    return WACT_NONE;
  }

  switch (screen_) {
    case WSCR_WIFI:
      if (detail_) {
        if (inRect(kBackBtn, x, y)) detail_ = false;
        return WACT_NONE;
      }
      if (inRect(kScanBtn, x, y)) return WACT_SCAN_WIFI;
      for (uint8_t i = 0; i < wifiCount_; ++i) {
        if (inRect(wifiCardRect(i), x, y)) {
          wifiSel_ = i;
          detail_ = true;
          return WACT_NONE;
        }
      }
      return WACT_NONE;
    case WSCR_BLE:
      if (inRect(kScanBtn, x, y)) return WACT_SCAN_BLE;
      return WACT_NONE;
    case WSCR_LOG:
      if (inRect(kPrevBtn, x, y)) pageLog(-1);
      else if (inRect(kNextBtn, x, y)) pageLog(+1);
      return WACT_NONE;
    case WSCR_QR:
    default:
      return WACT_NONE;
  }
}

void WirelessOpsUi::draw_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  g->fillScreen(kBg);
  switch (screen_) {
    case WSCR_WIFI:
      if (detail_ && wifiCount_ > 0) drawWifiDetail_(g);
      else { detail_ = false; drawWifiList_(g); }
      break;
    case WSCR_BLE: drawBle_(g); break;
    case WSCR_LOG: drawLog_(g); break;
    case WSCR_QR: drawQr_(g); break;
    default: break;
  }
  drawChrome_();
  CrowDisplay::flush();
}

void WirelessOpsUi::drawChrome_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  headerBar(g, "CYPHERDRIVE", "WIRELESS OPS", bannerText(), kAmber);
  static const char *const kLabels[WSCR_COUNT] = {"WI-FI", "BLE", "LOG", "QR"};
  tabBar(g, kLabels, WSCR_COUNT, (uint8_t)screen_, kAccent);
}

void WirelessOpsUi::drawWifiList_(Arduino_GFX *g) {
  text(g, kBodyX, 90, "NEARBY NETWORKS", fontL(), kTextHi, kLeft);
  char sub[48];
  snprintf(sub, sizeof(sub), "%u visible - passive receive, no join", wifiCount_);
  text(g, kBodyX, 120, sub, fontS(), kTextMut, kLeft);
  touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h, "RESCAN", false);

  if (wifiCount_ == 0) {
    panel(g, kBodyX, kGridY, kBodyW, 120, 12, kSurface, 1, kLine);
    text(g, kBodyX + 24, kGridY + 40, "No networks yet - tap RESCAN.", fontM(), kTextMut, kLeft);
    return;
  }

  for (uint8_t i = 0; i < wifiCount_; ++i) {
    Rect r = wifiCardRect(i);
    bool sel = (i == wifiSel_);
    panel(g, r.x, r.y, r.w, r.h, 12, sel ? kSurfaceHi : kSurface, 1, sel ? kAccent : kLine);

    String ssid = wifi_[i].hidden ? String("(hidden)") : wifi_[i].ssid;
    ssid = fitText(g, ssid, r.w - 100, fontL());
    text(g, r.x + 16, r.y + 10, ssid.c_str(), fontL(), kTextHi, kLeft);

    char cb[28];
    snprintf(cb, sizeof(cb), "ch %u - %s", wifi_[i].channel, bandName(wifi_[i].channel));
    text(g, r.x + 16, r.y + 36, cb, fontS(), kTextMut, kLeft);

    pill(g, r.x + 16, r.y + 52, wifi_[i].auth.c_str(), fontS(), kBg, securityColor(wifi_[i].auth));

    uint8_t lvl = rssiLevel(wifi_[i].rssi);
    signalBars(g, r.x + r.w - 16 - 43, r.y + 40, lvl, rssiColor(lvl));
    char rb[16];
    snprintf(rb, sizeof(rb), "%ld dBm", (long)wifi_[i].rssi);
    text(g, r.x + r.w - 16, r.y + 54, rb, fontS(), kTextMut, kRight);
  }
}

void WirelessOpsUi::drawWifiDetail_(Arduino_GFX *g) {
  const WifiNetworkRecord &n = wifi_[wifiSel_];
  touchButton(g, kBackBtn.x, kBackBtn.y, kBackBtn.w, kBackBtn.h, "< NETWORKS", false);
  text(g, kW - kBodyX, 96, "INSPECT", fontS(), kTextMut, kRight);

  const Rect card = {kBodyX, 140, kBodyW, 376};
  panel(g, card.x, card.y, card.w, card.h, 16, kSurface, 1, kLine);

  String ssid = n.hidden ? String("(hidden network)") : n.ssid;
  ssid = fitText(g, ssid, card.w - 320, fontXL());
  text(g, card.x + 32, card.y + 28, ssid.c_str(), fontXL(), kTextHi, kLeft);

  int16_t lx = card.x + 32, ly = card.y + 104;
  fieldBlock(g, lx, ly, "CHANNEL", String(n.channel));
  fieldBlock(g, lx + 200, ly, "BAND", bandName(n.channel));
  ly += 78;
  fieldBlock(g, lx, ly, "SECURITY", n.auth, securityColor(n.auth));
  fieldBlock(g, lx + 200, ly, "SSID", n.hidden ? String("hidden") : String("broadcast"));
  ly += 78;
  fieldBlock(g, lx, ly, "SIGNAL", String((long)n.rssi) + " dBm");

  // Signal arc-gauge on the right.
  int16_t gx = card.x + card.w - 190, gy = card.y + 170;
  uint8_t lvl = rssiLevel(n.rssi);
  float q = (float)(n.rssi + 100) / 70.0f;
  if (q < 0) q = 0;
  if (q > 1) q = 1;
  arcGauge(g, gx, gy, 118, 88, q, rssiColor(lvl));
  text(g, gx, gy - 24, String((long)n.rssi).c_str(), fontXL(), kTextHi, kCenter);
  text(g, gx, gy + 20, "dBm", fontS(), kTextMut, kCenter);
  signalBars(g, gx - 21, gy + 108, lvl, rssiColor(lvl));

  text(g, card.x + 32, card.y + card.h - 34,
       "Visibility only: no join, portal, deauth, HID, or capture.", fontS(), kTextMut, kLeft);
}

void WirelessOpsUi::drawBle_(Arduino_GFX *g) {
  text(g, kBodyX, 90, "BLE ADVERTISEMENTS", fontL(), kTextHi, kLeft);
  char sub[56];
  snprintf(sub, sizeof(sub), "%u frames - UART sidecar feed, parser only", bleCount_);
  text(g, kBodyX, 120, sub, fontS(), kTextMut, kLeft);
  touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h, "REFRESH", false);

  if (bleCount_ == 0) {
    panel(g, kBodyX, kGridY, kBodyW, 120, 12, kSurface, 1, kLine);
    text(g, kBodyX + 24, kGridY + 34, "No advertisements buffered.", fontM(), kTextMut, kLeft);
    text(g, kBodyX + 24, kGridY + 66, "Tap REFRESH or feed the UART sidecar.", fontS(), kTextMut,
         kLeft);
    return;
  }

  for (uint8_t i = 0; i < bleCount_; ++i) {
    Rect r = wifiCardRect(i);
    panel(g, r.x, r.y, r.w, r.h, 12, kSurface, 1, kLine);

    String label = ble_[i].label.length() ? ble_[i].label : String("(unnamed)");
    label = fitText(g, label, r.w - 100, fontL());
    text(g, r.x + 16, r.y + 10, label.c_str(), fontL(), kTextHi, kLeft);

    String addr = fitText(g, ble_[i].address, r.w - 100, fontS());
    text(g, r.x + 16, r.y + 36, addr.c_str(), fontS(), kTextMut, kLeft);

    String vendor = ble_[i].vendor.length() ? ble_[i].vendor : String("unknown");
    pill(g, r.x + 16, r.y + 52, vendor.c_str(), fontS(), kBg, kAccent);

    uint8_t lvl = rssiLevel(ble_[i].rssi);
    signalBars(g, r.x + r.w - 16 - 43, r.y + 40, lvl, rssiColor(lvl));
    char rb[16];
    snprintf(rb, sizeof(rb), "%ld dBm", (long)ble_[i].rssi);
    text(g, r.x + r.w - 16, r.y + 54, rb, fontS(), kTextMut, kRight);
  }
}

void WirelessOpsUi::drawLog_(Arduino_GFX *g) {
  uint8_t entries = log_ ? log_->count() : 0;
  uint8_t pages = logPageCount();
  text(g, kBodyX, 90, "SCAN LOG", fontL(), kTextHi, kLeft);
  char sub[48];
  snprintf(sub, sizeof(sub), "%u entries - local to the panel", entries);
  text(g, kBodyX, 120, sub, fontS(), kTextMut, kLeft);

  char pg[24];
  snprintf(pg, sizeof(pg), "PAGE %u/%u", logPage_ + 1, pages);
  text(g, (kPrevBtn.x + kNextBtn.x + kNextBtn.w) / 2, 96, pg, fontS(), kTextMut, kCenter);
  touchButton(g, kPrevBtn.x, kPrevBtn.y, kPrevBtn.w, kPrevBtn.h, "PREV", false);
  touchButton(g, kNextBtn.x, kNextBtn.y, kNextBtn.w, kNextBtn.h, "NEXT", false);

  if (entries == 0) {
    panel(g, kBodyX, kGridY, kBodyW, 120, 12, kSurface, 1, kLine);
    text(g, kBodyX + 24, kGridY + 40, "No log entries yet.", fontM(), kTextMut, kLeft);
    return;
  }

  const int16_t rowH = 58;
  const int16_t rowGap = 4;
  for (uint8_t k = 0; k < kLogRowsPerPage; ++k) {
    uint8_t idx = logPage_ * kLogRowsPerPage + k;
    ScanLog::Row row;
    if (!log_->rowFromNewest(idx, row)) break;
    int16_t y = kGridY + k * (rowH + rowGap);
    panel(g, kBodyX, y, kBodyW, rowH, 10, kSurface, 1, kLine);

    const char *tn = log_->typeName(row.type);
    pill(g, kBodyX + 14, y + 15, tn, fontS(), kBg, logTypeColor(row.type));

    char ts[16];
    snprintf(ts, sizeof(ts), "t+%.1fs", row.timestampMs / 1000.0);
    text(g, kBodyX + kBodyW - 16, y + 10, ts, fontS(), kTextMut, kRight);

    int16_t tx = kBodyX + 110;
    String summary = fitText(g, row.summary, kBodyW - 260, fontM());
    text(g, tx, y + 8, summary.c_str(), fontM(), kTextHi, kLeft);
    String detail = fitText(g, row.detail, kBodyW - 130, fontS());
    text(g, tx, y + 34, detail.c_str(), fontS(), kTextMut, kLeft);
  }
}

void WirelessOpsUi::drawQr_(Arduino_GFX *g) {
  text(g, kBodyX, 90, "QR HANDOFF", fontL(), kTextHi, kLeft);
  text(g, kBodyX, 120, "Pass this link to a phone - shown as text, nothing is transmitted.",
       fontS(), kTextMut, kLeft);

  const Rect card = {kBodyX, 140, kBodyW, 376};
  panel(g, card.x, card.y, card.w, card.h, 16, kSurface, 1, kLine);

  const int16_t qrSize = 300;
  drawQrPlaceholder(g, card.x + 28, card.y + 38, qrSize, qrUrl_);

  int16_t rx = card.x + 28 + qrSize + 40;
  int16_t rw = card.x + card.w - 32 - rx;
  text(g, rx, card.y + 40, "HANDOFF URL", fontS(), kTextMut, kLeft);
  drawWrapped(g, qrUrl_, rx, card.y + 70, rw, 34, 4, fontL(), kTextHi);

  const char *pstate = qrPersisted_ ? "SAVED (survives reboot)" : "VOLATILE (this boot only)";
  pill(g, rx, card.y + 230, pstate, fontS(), kBg, qrPersisted_ ? kGreen : kAmber);

  text(g, rx, card.y + 280, "DEFAULT", fontS(), kTextMut, kLeft);
  drawWrapped(g, qrDefault_, rx, card.y + 300, rw, 24, 2, fontS(), kTextMut);
  text(g, card.x + 32, card.y + card.h - 34,
       "Credential-like query fields (password, token, ssid) are rejected.", fontS(), kTextMut,
       kLeft);
}

#else  // ---------------- headless stubs ----------------

bool WirelessOpsUi::begin() { return false; }
void WirelessOpsUi::markDirty_() {}
WirelessAction WirelessOpsUi::tick() { return WACT_NONE; }

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
