#include "WirelessOpsUi.h"

#include <CrowPanelShared.h>

// ---------------------------------------------------------------------------
// Plain state + serial-parity navigation. These compile in BOTH the display and
// the headless build so `selftest`, `screen`, `net`, `ble`, `page`, and `touch`
// behave identically with no panel attached.
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
  wifiPage_ = 0;  // a fresh scan resets to the first page
  markDirty_();
}

void WirelessOpsUi::setLink(const WifiLinkStatus &link) {
  link_ = link;
  markDirty_();
}

void WirelessOpsUi::setCaptive(CaptivePortalResult result) {
  captive_ = result;
  markDirty_();
}

void WirelessOpsUi::setServices(const ServiceRecord *rows, uint8_t count) {
  serviceCount_ = count < kMaxServices ? count : kMaxServices;
  for (uint8_t i = 0; i < serviceCount_; ++i) services_[i] = rows[i];
  markDirty_();
}

void WirelessOpsUi::setPorts(const PortResult *rows, uint8_t count) {
  portCount_ = count < kMaxPorts ? count : kMaxPorts;
  for (uint8_t i = 0; i < portCount_; ++i) ports_[i] = rows[i];
  markDirty_();
}

void WirelessOpsUi::setBle(const BleDeviceRecord *rows, uint8_t count) {
  bleCount_ = count < kMaxBle ? count : kMaxBle;
  for (uint8_t i = 0; i < bleCount_; ++i) ble_[i] = rows[i];
  if (bleCount_ == 0) {
    bleSel_ = 0;
  } else if (bleSel_ >= bleCount_) {
    bleSel_ = bleCount_ - 1;
  }
  blePage_ = 0;  // a fresh scan resets to the first page
  markDirty_();
}

void WirelessOpsUi::setBleServices(const BleServiceRecord *rows, uint8_t count, const String &addr) {
  bleServiceCount_ = count < kMaxBleServices ? count : kMaxBleServices;
  for (uint8_t i = 0; i < bleServiceCount_; ++i) bleServices_[i] = rows[i];
  bleConnectedAddr_ = addr;
  markDirty_();
}

void WirelessOpsUi::setLog(const ScanLog *log) {
  log_ = log;
  clampLogPage_();
  markDirty_();
}

void WirelessOpsUi::setStatus(const String &status) {
  status_ = status;
  markDirty_();
}

void WirelessOpsUi::setPayloads(const String *names, uint8_t count, uint8_t presetCount) {
  payloadCount_ = count < kMaxPayloads ? count : kMaxPayloads;
  for (uint8_t i = 0; i < payloadCount_; ++i) payloads_[i] = names[i];
  payloadPresetCount_ = presetCount;
  if (payloadPage_ * kCardsPerPage >= payloadCount_) payloadPage_ = 0;
  markDirty_();
}

void WirelessOpsUi::setPayloadStatus(const String &name, uint8_t pct, bool running) {
  if (name == payloadRunName_ && pct == payloadPct_ && running == payloadRunning_) return;
  payloadRunName_ = name;
  payloadPct_ = pct;
  payloadRunning_ = running;
  markDirty_();
}

void WirelessOpsUi::showToolResult(const String &title, const String *lines, uint8_t count) {
  toolTitle_ = title;
  toolLineCount_ = count < kMaxToolLines ? count : kMaxToolLines;
  for (uint8_t i = 0; i < toolLineCount_; ++i) toolLines_[i] = lines[i];
  toolModal_ = true;
  markDirty_();
}

const char *WirelessOpsUi::screenName() const {
  switch (screen_) {
    case WSCR_WIFI: return detail_ ? "wifi-detail" : "wifi";
    case WSCR_BLE: return "ble";
    case WSCR_HID: return "hid";
    case WSCR_PAYLOAD: return "payload";
    case WSCR_LOG: return "log";
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
  else if (n == "hid") next = WSCR_HID;
  else if (n == "pld" || n == "payload" || n == "payloads") next = WSCR_PAYLOAD;
  else if (n == "log" || n == "logs") next = WSCR_LOG;
  else return false;
  screen_ = next;
  detail_ = false;
  markDirty_();
  return true;
}

bool WirelessOpsUi::selectNetwork(uint8_t index) {
  if (index >= wifiCount_) return false;
  wifiSel_ = index;
  wifiPage_ = index / kCardsPerPage;  // flip to the page holding the selection
  screen_ = WSCR_WIFI;
  detail_ = true;
  markDirty_();
  return true;
}

bool WirelessOpsUi::selectBle(uint8_t index) {
  if (index >= bleCount_) return false;
  bleSel_ = index;
  blePage_ = index / kCardsPerPage;
  screen_ = WSCR_BLE;
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
  out.print(F(" ble_sel="));
  out.print(bleSel_);
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
constexpr int16_t kBodyX = 24;
constexpr int16_t kBodyW = kW - 2 * kBodyX;  // 976

constexpr int16_t kActionY = 84;
constexpr int16_t kActionH = 40;
constexpr int16_t kLinkY = 132;
constexpr int16_t kLinkH = 42;

constexpr int16_t kGridY = 186;
constexpr int16_t kCardGap = 16;
constexpr int16_t kCardW = (kBodyW - kCardGap) / 2;  // 480
constexpr int16_t kCardH = 80;
constexpr int16_t kCardVGap = 10;

constexpr int16_t kToolY = 480;
constexpr int16_t kToolH = 46;

struct Rect {
  int16_t x, y, w, h;
};
bool inRect(const Rect &r, int16_t x, int16_t y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Top action-strip buttons (identical rects for draw + hit-test).
const Rect kScanBtn = {kW - kBodyX - 150, kActionY, 150, kActionH};
const Rect kSecondBtn = {kW - kBodyX - 150 - 12 - 150, kActionY, 150, kActionH};
const Rect kBackBtn = {kBodyX, kActionY, 160, kActionH};
const Rect kJoinBtn = {kW - kBodyX - 220, 452, 220, 52};
// Log paging.
const Rect kNextBtn = {kW - kBodyX - 120, kActionY, 120, kActionH};
const Rect kPrevBtn = {kW - kBodyX - 120 - 12 - 120, kActionY, 120, kActionH};

Rect cardRect(uint8_t i) {
  int16_t col = i % 2, row = i / 2;
  return {int16_t(kBodyX + col * (kCardW + kCardGap)),
          int16_t(kGridY + row * (kCardH + kCardVGap)), kCardW, kCardH};
}

// Four client-tool buttons across the bottom of the WIFI screen.
Rect toolRect(uint8_t i) {
  const int16_t gap = 10;
  const int16_t w = (kBodyW - 3 * gap) / 4;  // ~236
  return {int16_t(kBodyX + i * (w + gap)), kToolY, w, kToolH};
}

// List pagination controls (shared by the WI-FI and BLE list views), tucked to
// the left of the SCAN/LEAVE cluster in the top action row.
constexpr uint8_t kCardsPerPageC = 6;  // must match WirelessOpsUi::kCardsPerPage
uint8_t pageCountOf(uint8_t n) { return n ? (uint8_t)((n + kCardsPerPageC - 1) / kCardsPerPageC) : 1; }
const Rect kPagePrevBtn = {int16_t(kSecondBtn.x - 172), kActionY, 46, kActionH};
const Rect kPageNextBtn = {int16_t(kSecondBtn.x - 58), kActionY, 46, kActionH};
// SAVE-to-SD: on the Wi-Fi inspector (beside JOIN) and the BLE list (bottom-left).
const Rect kSaveDetailBtn = {int16_t(kJoinBtn.x - 12 - 200), kJoinBtn.y, 200, kJoinBtn.h};
const Rect kBleSaveBtn = {kBodyX, kToolY, 240, kToolH};

// Payload list rows (full width, 6 per page).
Rect payloadRowRect(uint8_t i) {
  const int16_t rowH = 46, gap = 6, y0 = 186;
  return {kBodyX, int16_t(y0 + i * (rowH + gap)), kBodyW, rowH};
}

// HID macro tiles: 2 rows x 4.
constexpr int16_t kHidGridY = 156;
constexpr int16_t kHidGap = 12;
constexpr int16_t kHidTileW = (kBodyW - 3 * kHidGap) / 4;  // ~235
constexpr int16_t kHidTileH = 118;
constexpr int16_t kHidVGap = 16;
Rect hidTileRect(uint8_t i) {
  int16_t col = i % 4, row = i / 4;
  return {int16_t(kBodyX + col * (kHidTileW + kHidGap)),
          int16_t(kHidGridY + row * (kHidTileH + kHidVGap)), kHidTileW, kHidTileH};
}
const Rect kHidToggleBtn = {kW - kBodyX - 200, kActionY, 200, kActionH};

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
const char *bandName(uint8_t channel) { return channel >= 32 ? "5 GHz" : "2.4 GHz"; }

uint16_t securityColor(const String &auth) {
  String a = auth;
  a.toLowerCase();
  if (a.indexOf("wpa3") >= 0 || a.indexOf("wpa2") >= 0) return kGreen;
  if (a == "open" || a.indexOf("wep") >= 0) return kRed;
  return kAmber;  // plain wpa / unknown
}

uint16_t linkColor(WifiLinkState s) {
  switch (s) {
    case WLINK_CONNECTED: return kGreen;
    case WLINK_JOINING: return kAmber;
    case WLINK_FAILED: return kRed;
    default: return kTextMut;
  }
}

uint16_t captiveColor(CaptivePortalResult c) {
  switch (c) {
    case CAPTIVE_CLEAR: return kGreen;
    case CAPTIVE_PORTAL: return kAmber;
    case CAPTIVE_OFFLINE: return kRed;
    default: return kTextMut;
  }
}
const char *captiveName(CaptivePortalResult c) {
  switch (c) {
    case CAPTIVE_CLEAR: return "clear (204)";
    case CAPTIVE_PORTAL: return "portal!";
    case CAPTIVE_OFFLINE: return "offline";
    default: return "unknown";
  }
}

uint16_t logTypeColor(ScanLog::EntryType type) {
  switch (type) {
    case ScanLog::kWifi: return kAccent;
    case ScanLog::kBle: return rgb(0x8B, 0x5C, 0xF6);  // violet
    case ScanLog::kNet: return kGreen;
    case ScanLog::kHid: return kAmber;
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

void fieldBlock(Arduino_GFX *g, int16_t x, int16_t y, const char *label, const String &value,
                uint16_t valueColor = kTextHi) {
  text(g, x, y, label, fontS(), kTextMut, kLeft);
  text(g, x, y + 20, value.c_str(), fontL(), valueColor, kLeft);
}

// "< 2/4 >" pager drawn in the top action row when a list spans multiple pages.
void drawPager(Arduino_GFX *g, uint8_t page, uint8_t pages) {
  touchButton(g, kPagePrevBtn.x, kPagePrevBtn.y, kPagePrevBtn.w, kPagePrevBtn.h, "<", false);
  touchButton(g, kPageNextBtn.x, kPageNextBtn.y, kPageNextBtn.w, kPageNextBtn.h, ">", false);
  char pl[12];
  snprintf(pl, sizeof(pl), "%u/%u", page + 1, pages);
  int16_t cx = (int16_t)((kPagePrevBtn.x + kPagePrevBtn.w + kPageNextBtn.x) / 2);
  text(g, cx, kActionY + 14, pl, fontS(), kTextHi, kCenter);
}

}  // namespace

bool WirelessOpsUi::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "CYPHERDRIVE FIELD TOOL",
                              /*manualFlush=*/true) &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
  return ready_;
}

void WirelessOpsUi::markDirty_() { dirty_ = true; }

WirelessEvent WirelessOpsUi::tick() {
  touch_.tick();
  WirelessEvent ev;
  if (touch_.releasedEdge()) {
    if (kbdActive_) ev = handleKeyboard_(touch_.releaseX(), touch_.releaseY());
    else if (toolModal_) toolModal_ = false;  // any tap dismisses the results popup
    else ev = handleRelease_(touch_.releaseX(), touch_.releaseY());
    dirty_ = true;
  }
  if (ready_ && dirty_) {
    draw_();
    dirty_ = false;
  }
  return ev;
}

WirelessEvent WirelessOpsUi::handleKeyboard_(int16_t x, int16_t y) {
  WirelessEvent ev;
  KbEvent k = kbd_.hitTest(x, y);
  switch (k.action) {
    case KB_CHAR:
      if (kbdBuffer_.length() < 63) kbdBuffer_ += k.ch;
      break;
    case KB_BACKSPACE:
      if (kbdBuffer_.length()) kbdBuffer_.remove(kbdBuffer_.length() - 1);
      break;
    case KB_ENTER:
      joinPassword_ = kbdBuffer_;
      kbdActive_ = false;
      ev.action = WACT_JOIN_WIFI;
      ev.index = (int8_t)wifiSel_;
      break;
    case KB_CANCEL:
      kbdActive_ = false;
      break;
    default:  // shift / symbols just repaint
      break;
  }
  return ev;
}

WirelessEvent WirelessOpsUi::handleRelease_(int16_t x, int16_t y) {
  WirelessEvent ev;
  // Bottom tab strip wins everywhere.
  int8_t tab = tabHit(x, y, WSCR_COUNT);
  if (tab >= 0) {
    screen_ = (WirelessScreen)tab;
    detail_ = false;
    return ev;
  }

  switch (screen_) {
    case WSCR_WIFI:
      if (detail_) {
        if (inRect(kBackBtn, x, y)) { detail_ = false; return ev; }
        if (inRect(kJoinBtn, x, y)) {
          if (wifiSel_ < wifiCount_ && !wifi_[wifiSel_].open()) {
            // Secured: open the on-screen keyboard to enter the key.
            kbdActive_ = true;
            kbdBuffer_ = "";
            kbd_.reset();
            return ev;  // JOIN fires from the keyboard's ENTER
          }
          joinPassword_ = "";  // open network
          ev.action = WACT_JOIN_WIFI;
          ev.index = (int8_t)wifiSel_;
          return ev;
        }
        if (inRect(kSaveDetailBtn, x, y)) { ev.action = WACT_SAVE_WIFI; ev.index = (int8_t)wifiSel_; return ev; }
        return ev;
      }
      if (inRect(kScanBtn, x, y)) { ev.action = WACT_SCAN_WIFI; return ev; }
      if (inRect(kSecondBtn, x, y)) { ev.action = WACT_LEAVE_WIFI; return ev; }
      if (inRect(toolRect(0), x, y)) { ev.action = WACT_TOOL_CAPTIVE; return ev; }
      if (inRect(toolRect(1), x, y)) { ev.action = WACT_TOOL_MDNS; return ev; }
      if (inRect(toolRect(2), x, y)) { ev.action = WACT_TOOL_PORTSCAN; return ev; }
      if (inRect(toolRect(3), x, y)) { ev.action = WACT_TOOL_SWEEP; return ev; }
      {
        uint8_t pages = pageCountOf(wifiCount_);
        if (pages > 1 && inRect(kPagePrevBtn, x, y)) {
          wifiPage_ = wifiPage_ ? (uint8_t)(wifiPage_ - 1) : (uint8_t)(pages - 1);
          return ev;
        }
        if (pages > 1 && inRect(kPageNextBtn, x, y)) {
          wifiPage_ = (uint8_t)((wifiPage_ + 1) % pages);
          return ev;
        }
      }
      for (uint8_t i = 0; i < kCardsPerPage; ++i) {
        uint8_t idx = (uint8_t)(wifiPage_ * kCardsPerPage + i);
        if (idx >= wifiCount_) break;
        if (inRect(cardRect(i), x, y)) { wifiSel_ = idx; detail_ = true; return ev; }
      }
      return ev;
    case WSCR_BLE:
      if (inRect(kScanBtn, x, y)) { ev.action = WACT_SCAN_BLE; return ev; }
      if (bleConnectedAddr_.length() > 0) {
        if (inRect(kSecondBtn, x, y)) { ev.action = WACT_DISCONNECT_BLE; return ev; }
        return ev;
      }
      if (inRect(kSecondBtn, x, y)) { ev.action = WACT_CONNECT_BLE; ev.index = (int8_t)bleSel_; return ev; }
      if (inRect(kBleSaveBtn, x, y)) { ev.action = WACT_SAVE_BLE; ev.index = (int8_t)bleSel_; return ev; }
      {
        uint8_t pages = pageCountOf(bleCount_);
        if (pages > 1 && inRect(kPagePrevBtn, x, y)) {
          blePage_ = blePage_ ? (uint8_t)(blePage_ - 1) : (uint8_t)(pages - 1);
          return ev;
        }
        if (pages > 1 && inRect(kPageNextBtn, x, y)) {
          blePage_ = (uint8_t)((blePage_ + 1) % pages);
          return ev;
        }
      }
      for (uint8_t i = 0; i < kCardsPerPage; ++i) {
        uint8_t idx = (uint8_t)(blePage_ * kCardsPerPage + i);
        if (idx >= bleCount_) break;
        if (inRect(cardRect(i), x, y)) { bleSel_ = idx; return ev; }
      }
      return ev;
    case WSCR_HID:
      if (inRect(kHidToggleBtn, x, y)) { ev.action = WACT_HID_OUTPUT_TOGGLE; return ev; }
      for (uint8_t i = 0; i < HidPad::kSlots; ++i) {
        if (inRect(hidTileRect(i), x, y)) { ev.action = WACT_HID_SLOT; ev.index = (int8_t)i; return ev; }
      }
      return ev;
    case WSCR_PAYLOAD: {
      if (payloadRunning_ && inRect(kScanBtn, x, y)) { ev.action = WACT_STOP_PAYLOAD; return ev; }
      uint8_t pages = pageCountOf(payloadCount_);
      if (pages > 1 && inRect(kPagePrevBtn, x, y)) {
        payloadPage_ = payloadPage_ ? (uint8_t)(payloadPage_ - 1) : (uint8_t)(pages - 1);
        return ev;
      }
      if (pages > 1 && inRect(kPageNextBtn, x, y)) {
        payloadPage_ = (uint8_t)((payloadPage_ + 1) % pages);
        return ev;
      }
      for (uint8_t i = 0; i < kCardsPerPage; ++i) {
        uint8_t idx = (uint8_t)(payloadPage_ * kCardsPerPage + i);
        if (idx >= payloadCount_) break;
        if (inRect(payloadRowRect(i), x, y)) {
          ev.action = WACT_RUN_PAYLOAD;
          ev.index = (int8_t)idx;
          return ev;
        }
      }
      return ev;
    }
    case WSCR_LOG:
      if (inRect(kPrevBtn, x, y)) pageLog(-1);
      else if (inRect(kNextBtn, x, y)) pageLog(+1);
      return ev;
    default:
      return ev;
  }
}

void WirelessOpsUi::draw_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  g->fillScreen(kBg);
  if (kbdActive_) {
    drawKeyboard_(g);
    CrowDisplay::flush();
    return;
  }
  if (toolModal_) {
    drawToolResult_(g);
    CrowDisplay::flush();
    return;
  }
  switch (screen_) {
    case WSCR_WIFI: drawWifi_(g); break;
    case WSCR_BLE: drawBle_(g); break;
    case WSCR_HID: drawHid_(g); break;
    case WSCR_PAYLOAD: drawPayload_(g); break;
    case WSCR_LOG: drawLog_(g); break;
    default: break;
  }
  drawChrome_();
  CrowDisplay::flush();
}

void WirelessOpsUi::drawChrome_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  headerBar(g, "CYPHERDRIVE", "ACTIVE FIELD TOOL", status_.c_str(), kAccent);
  static const char *const kLabels[WSCR_COUNT] = {"WI-FI", "BLE", "HID", "PLD", "LOG"};
  tabBar(g, kLabels, WSCR_COUNT, (uint8_t)screen_, kAccent);
}

void WirelessOpsUi::drawKeyboard_(Arduino_GFX *g) {
  const WifiNetworkRecord &n = wifi_[wifiSel_ < wifiCount_ ? wifiSel_ : 0];
  headerBar(g, "JOIN NETWORK", n.ssid.length() ? n.ssid.c_str() : "(hidden)", "ENTER KEY",
            kAccent);
  panel(g, kBodyX, 92, kBodyW, 56, 10, kSurface, 1, kLine);
  String masked;
  for (uint16_t i = 0; i < kbdBuffer_.length(); ++i) masked += "*";
  bool empty = masked.length() == 0;
  if (empty) masked = "(type the Wi-Fi password)";
  text(g, kBodyX + 20, 110, masked.c_str(), fontL(), empty ? kTextMut : kTextHi, kLeft);
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%u chars", (unsigned)kbdBuffer_.length());
  text(g, kBodyX + kBodyW - 16, 112, cnt, fontS(), kTextMut, kRight);
  text(g, kBodyX, 176, "Tap ENTER to join, CANCEL to go back.", fontS(), kTextMut, kLeft);
  kbd_.draw(g);
}

void WirelessOpsUi::drawToolResult_(Arduino_GFX *g) {
  headerBar(g, "RESULT", toolTitle_.length() ? toolTitle_.c_str() : "tool", "TAP TO CLOSE",
            kAccent);
  const Rect card = {kBodyX, 92, kBodyW, (int16_t)(kChromeTabY - 92 - 12)};
  panel(g, card.x, card.y, card.w, card.h, 14, kSurface, 1, kLine);
  if (toolLineCount_ == 0) {
    text(g, card.x + 24, card.y + 44, "(no results)", fontM(), kTextMut, kLeft);
  } else {
    int16_t ly = card.y + 26;
    for (uint8_t i = 0; i < toolLineCount_; ++i) {
      text(g, card.x + 24, ly, fitText(g, toolLines_[i], card.w - 48, fontM()).c_str(), fontM(),
           kTextHi, kLeft);
      ly += 30;
    }
  }
  text(g, card.x + card.w / 2, card.y + card.h - 28, "tap anywhere to close", fontS(), kTextMut,
       kCenter);
}

void WirelessOpsUi::showBusy(const String &label) {
  if (!ready_) return;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  const int16_t w = 540, h = 130;
  const int16_t x = (kChromeW - w) / 2, y = (kChromeH - h) / 2;
  panel(g, x, y, w, h, 16, kSurfaceHi, 2, kAccent);
  text(g, x + w / 2, y + 44, (String("Running ") + label + "...").c_str(), fontL(), kTextHi,
       kCenter);
  text(g, x + w / 2, y + 84, "please wait", fontS(), kTextMut, kCenter);
  CrowDisplay::flush();
}

void WirelessOpsUi::drawWifi_(Arduino_GFX *g) {
  if (detail_ && wifiCount_ > 0) {
    const WifiNetworkRecord &n = wifi_[wifiSel_];
    touchButton(g, kBackBtn.x, kBackBtn.y, kBackBtn.w, kBackBtn.h, "< NETWORKS", false);
    text(g, kW - kBodyX, 96, "INSPECT / JOIN", fontS(), kTextMut, kRight);

    const Rect card = {kBodyX, 140, kBodyW, 300};
    panel(g, card.x, card.y, card.w, card.h, 16, kSurface, 1, kLine);
    String ssid = n.hidden ? String("(hidden network)") : n.ssid;
    ssid = fitText(g, ssid, card.w - 320, fontXL());
    text(g, card.x + 32, card.y + 26, ssid.c_str(), fontXL(), kTextHi, kLeft);

    int16_t lx = card.x + 32, ly = card.y + 92;
    fieldBlock(g, lx, ly, "CHANNEL", String(n.channel));
    fieldBlock(g, lx + 190, ly, "BAND", bandName(n.channel));
    fieldBlock(g, lx + 380, ly, "SECURITY", n.auth, securityColor(n.auth));
    fieldBlock(g, lx + 610, ly, "PHY", n.phy.length() ? ("802.11" + n.phy) : String("-"));
    ly += 76;
    fieldBlock(g, lx, ly, "BSSID", n.bssid.length() ? n.bssid : String("-"));
    fieldBlock(g, lx + 380, ly, "SIGNAL", String((long)n.rssi) + " dBm");
    fieldBlock(g, lx + 610, ly, "WPS", n.wps ? String("yes") : String("no"),
               n.wps ? kAmber : kTextHi);

    // Signal arc-gauge on the right.
    int16_t gx = card.x + card.w - 150, gy = card.y + 150;
    uint8_t lvl = rssiLevel(n.rssi);
    float q = (float)(n.rssi + 100) / 70.0f;
    if (q < 0) q = 0;
    if (q > 1) q = 1;
    arcGauge(g, gx, gy, 96, 72, q, rssiColor(lvl));
    text(g, gx, gy - 14, String((long)n.rssi).c_str(), fontL(), kTextHi, kCenter);

    const char *label = n.open() ? "JOIN (OPEN)" : "JOIN";
    touchButton(g, kJoinBtn.x, kJoinBtn.y, kJoinBtn.w, kJoinBtn.h, label, true, kGreen);
    touchButton(g, kSaveDetailBtn.x, kSaveDetailBtn.y, kSaveDetailBtn.w, kSaveDetailBtn.h,
                "SAVE TO SD", false);
    text(g, kBodyX, kJoinBtn.y + 18,
         n.open() ? "Open network - keyboard optional." : "Tap JOIN to enter the key.",
         fontS(), kTextMut, kLeft);
    return;
  }

  char title[32];
  snprintf(title, sizeof(title), "NEARBY NETWORKS (%u)", wifiCount_);
  text(g, kBodyX, 96, title, fontL(), kTextHi, kLeft);
  touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h, "SCAN", false);
  touchButton(g, kSecondBtn.x, kSecondBtn.y, kSecondBtn.w, kSecondBtn.h, "LEAVE", false);
  if (pageCountOf(wifiCount_) > 1) drawPager(g, wifiPage_, pageCountOf(wifiCount_));

  // Link status strip.
  panel(g, kBodyX, kLinkY, kBodyW, kLinkH, 10, kSurface, 1, kLine);
  statusDot(g, kBodyX + 18, kLinkY + kLinkH / 2, 7, linkColor(link_.state));
  String linkText = String("LINK: ") + link_.stateName();
  if (link_.ssid.length()) linkText += " " + link_.ssid;
  if (link_.ip.length()) linkText += "  ip " + link_.ip;
  if (link_.gateway.length()) linkText += "  gw " + link_.gateway;
  text(g, kBodyX + 40, kLinkY + 14, fitText(g, linkText, kBodyW - 240, fontM()).c_str(), fontM(),
       kTextHi, kLeft);
  if (captive_ != CAPTIVE_UNKNOWN) {
    pill(g, kBodyX + kBodyW - 180, kLinkY + 10, captiveName(captive_), fontS(), kBg,
         captiveColor(captive_));
  }

  if (wifiCount_ == 0) {
    panel(g, kBodyX, kGridY, kBodyW, 120, 12, kSurface, 1, kLine);
    text(g, kBodyX + 24, kGridY + 40, "No networks yet - tap SCAN.", fontM(), kTextMut, kLeft);
  } else {
    for (uint8_t i = 0; i < kCardsPerPage; ++i) {
      uint8_t idx = (uint8_t)(wifiPage_ * kCardsPerPage + i);
      if (idx >= wifiCount_) break;
      Rect r = cardRect(i);
      bool sel = (idx == wifiSel_);
      panel(g, r.x, r.y, r.w, r.h, 12, sel ? kSurfaceHi : kSurface, 1, sel ? kAccent : kLine);
      String ssid = wifi_[idx].hidden ? String("(hidden)") : wifi_[idx].ssid;
      ssid = fitText(g, ssid, r.w - 110, fontL());
      text(g, r.x + 16, r.y + 10, ssid.c_str(), fontL(), kTextHi, kLeft);
      char cb[40];
      snprintf(cb, sizeof(cb), "ch %u - %s%s%s", wifi_[idx].channel, bandName(wifi_[idx].channel),
               wifi_[idx].phy.length() ? " - " : "", wifi_[idx].phy.c_str());
      text(g, r.x + 16, r.y + 36, cb, fontS(), kTextMut, kLeft);
      pill(g, r.x + 16, r.y + 52, wifi_[idx].auth.c_str(), fontS(), kBg, securityColor(wifi_[idx].auth));
      uint8_t lvl = rssiLevel(wifi_[idx].rssi);
      signalBars(g, r.x + r.w - 16 - 43, r.y + 38, lvl, rssiColor(lvl));
      char rb[16];
      snprintf(rb, sizeof(rb), "%ld dBm", (long)wifi_[idx].rssi);
      text(g, r.x + r.w - 16, r.y + 54, rb, fontS(), kTextMut, kRight);
    }
  }

  // Client-tool buttons operate on the joined link.
  touchButton(g, toolRect(0).x, toolRect(0).y, toolRect(0).w, toolRect(0).h, "CAPTIVE", false);
  touchButton(g, toolRect(1).x, toolRect(1).y, toolRect(1).w, toolRect(1).h, "mDNS", false);
  touchButton(g, toolRect(2).x, toolRect(2).y, toolRect(2).w, toolRect(2).h, "PORTSCAN", false);
  touchButton(g, toolRect(3).x, toolRect(3).y, toolRect(3).w, toolRect(3).h, "SWEEP", false);
}

void WirelessOpsUi::drawBle_(Arduino_GFX *g) {
  bool connected = bleConnectedAddr_.length() > 0;
  char btitle[28];
  snprintf(btitle, sizeof(btitle), "BLE DEVICES (%u)", bleCount_);
  text(g, kBodyX, 96, connected ? "BLE - CONNECTED" : btitle, fontL(), kTextHi, kLeft);
  touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h, "SCAN", false);
  touchButton(g, kSecondBtn.x, kSecondBtn.y, kSecondBtn.w, kSecondBtn.h,
              connected ? "DISCONNECT" : "CONNECT", false, connected ? kAmber : kAccent);
  if (!connected && pageCountOf(bleCount_) > 1) drawPager(g, blePage_, pageCountOf(bleCount_));

  // Connection strip.
  panel(g, kBodyX, kLinkY, kBodyW, kLinkH, 10, kSurface, 1, kLine);
  statusDot(g, kBodyX + 18, kLinkY + kLinkH / 2, 7, connected ? kGreen : kTextMut);
  String s = connected ? (String("GATT ") + bleConnectedAddr_)
                       : (String(bleCount_) + " devices - on-panel C6 central");
  text(g, kBodyX + 40, kLinkY + 14, fitText(g, s, kBodyW - 80, fontM()).c_str(), fontM(), kTextHi,
       kLeft);

  if (connected) {
    if (bleServiceCount_ == 0) {
      panel(g, kBodyX, kGridY, kBodyW, 100, 12, kSurface, 1, kLine);
      text(g, kBodyX + 24, kGridY + 40, "No GATT services enumerated.", fontM(), kTextMut, kLeft);
      return;
    }
    const int16_t rowH = 46;
    for (uint8_t i = 0; i < bleServiceCount_; ++i) {
      int16_t y = kGridY + i * (rowH + 6);
      panel(g, kBodyX, y, kBodyW, rowH, 10, kSurface, 1, kLine);
      text(g, kBodyX + 16, y + 15, fitText(g, bleServices_[i].uuid, 260, fontM()).c_str(), fontM(),
           kTextHi, kLeft);
      if (bleServices_[i].label.length())
        text(g, kBodyX + 300, y + 16, bleServices_[i].label.c_str(), fontS(), kAccent, kLeft);
      char cc[20];
      snprintf(cc, sizeof(cc), "%u chars", bleServices_[i].charCount);
      text(g, kBodyX + kBodyW - 16, y + 16, cc, fontS(), kTextMut, kRight);
    }
    return;
  }

  if (bleCount_ == 0) {
    panel(g, kBodyX, kGridY, kBodyW, 120, 12, kSurface, 1, kLine);
    text(g, kBodyX + 24, kGridY + 40, "No devices - tap SCAN.", fontM(), kTextMut, kLeft);
    return;
  }
  for (uint8_t i = 0; i < kCardsPerPage; ++i) {
    uint8_t idx = (uint8_t)(blePage_ * kCardsPerPage + i);
    if (idx >= bleCount_) break;
    Rect r = cardRect(i);
    bool sel = (idx == bleSel_);
    panel(g, r.x, r.y, r.w, r.h, 12, sel ? kSurfaceHi : kSurface, 1, sel ? kAccent : kLine);
    String label = ble_[idx].name.length() ? ble_[idx].name : String("(unnamed)");
    label = fitText(g, label, r.w - 110, fontL());
    text(g, r.x + 16, r.y + 10, label.c_str(), fontL(), kTextHi, kLeft);
    String addrLine = ble_[idx].address;
    if (ble_[idx].addrType.length()) addrLine += "  " + ble_[idx].addrType;
    if (ble_[idx].txPower) addrLine += "  txp " + String(ble_[idx].txPower);
    text(g, r.x + 16, r.y + 36, fitText(g, addrLine, r.w - 110, fontS()).c_str(), fontS(),
         kTextMut, kLeft);
    String vendor = ble_[idx].vendor.length() ? ble_[idx].vendor : String("unknown");
    pill(g, r.x + 16, r.y + 52, vendor.c_str(), fontS(), kBg,
         ble_[idx].connectable ? kGreen : kTextMut);
    uint8_t lvl = rssiLevel(ble_[idx].rssi);
    signalBars(g, r.x + r.w - 16 - 43, r.y + 38, lvl, rssiColor(lvl));
    char rb[16];
    snprintf(rb, sizeof(rb), "%ld dBm", (long)ble_[idx].rssi);
    text(g, r.x + r.w - 16, r.y + 54, rb, fontS(), kTextMut, kRight);
  }
  touchButton(g, kBleSaveBtn.x, kBleSaveBtn.y, kBleSaveBtn.w, kBleSaveBtn.h, "SAVE TO SD", false);
}

void WirelessOpsUi::drawHid_(Arduino_GFX *g) {
  const char *mode = hid_ ? hid_->modeLabel() : "MOCK";
  bool live = hid_ ? hid_->live() : false;
  text(g, kBodyX, 96, "HID MACRO PAD", fontL(), kTextHi, kLeft);
  char ob[40];
  snprintf(ob, sizeof(ob), "OUTPUT: %s  %s", mode, live ? "(live)" : "(mock)");
  text(g, kBodyX, 122, ob, fontS(), live ? kGreen : kTextMut, kLeft);
  touchButton(g, kHidToggleBtn.x, kHidToggleBtn.y, kHidToggleBtn.w, kHidToggleBtn.h,
              "TOGGLE USB/BLE", false);

  for (uint8_t i = 0; i < HidPad::kSlots; ++i) {
    Rect r = hidTileRect(i);
    panel(g, r.x, r.y, r.w, r.h, 12, kSurface, 1, kLine);
    const char *lbl = hid_ ? hid_->slot(i).label : "";
    text(g, r.x + r.w / 2, r.y + r.h / 2 - 6, (lbl && lbl[0]) ? lbl : "-", fontL(), kTextHi,
         kCenter);
  }

  // Last action readout.
  int16_t ly = kHidGridY + 2 * (kHidTileH + kHidVGap) - kHidVGap + 8;
  panel(g, kBodyX, ly, kBodyW, 44, 10, kSurface, 1, kLine);
  String last = hid_ ? hid_->lastAction() : String("(none)");
  text(g, kBodyX + 16, ly + 15, ("last: " + fitText(g, last, kBodyW - 60, fontM())).c_str(),
       fontM(), kTextMut, kLeft);
}

void WirelessOpsUi::drawPayload_(Arduino_GFX *g) {
  char title[32];
  snprintf(title, sizeof(title), "HID PAYLOADS (%u)", payloadCount_);
  text(g, kBodyX, 96, title, fontL(), kTextHi, kLeft);
  if (payloadRunning_)
    touchButton(g, kScanBtn.x, kScanBtn.y, kScanBtn.w, kScanBtn.h, "STOP", false, kAmber);
  if (pageCountOf(payloadCount_) > 1) drawPager(g, payloadPage_, pageCountOf(payloadCount_));

  panel(g, kBodyX, kLinkY, kBodyW, kLinkH, 10, kSurface, 1, kLine);
  if (payloadRunning_) {
    String s = String("running: ") + payloadRunName_ + "  " + payloadPct_ + "%";
    text(g, kBodyX + 16, kLinkY + 14, fitText(g, s, kBodyW - 240, fontM()).c_str(), fontM(),
         kGreen, kLeft);
    int16_t bx = kBodyX + kBodyW - 216, bw = 200;
    g->drawRoundRect(bx, kLinkY + 12, bw, 18, 6, kLine);
    g->fillRoundRect(bx, kLinkY + 12, (int16_t)(bw * payloadPct_ / 100), 18, 6, kGreen);
  } else {
    text(g, kBodyX + 16, kLinkY + 14, "Tap a payload to type it over the active HID output.",
         fontM(), kTextMut, kLeft);
  }

  if (payloadCount_ == 0) {
    panel(g, kBodyX, 186, kBodyW, 100, 12, kSurface, 1, kLine);
    text(g, kBodyX + 24, 222, "No payloads. Presets are built in; add .txt DuckyScript to",
         fontM(), kTextMut, kLeft);
    text(g, kBodyX + 24, 250, "/cypherdrive/payloads on the SD card.", fontS(), kTextMut, kLeft);
    return;
  }

  for (uint8_t i = 0; i < kCardsPerPage; ++i) {
    uint8_t idx = (uint8_t)(payloadPage_ * kCardsPerPage + i);
    if (idx >= payloadCount_) break;
    Rect r = payloadRowRect(i);
    bool isPreset = idx < payloadPresetCount_;
    panel(g, r.x, r.y, r.w, r.h, 10, kSurface, 1, kLine);
    text(g, r.x + 16, r.y + 14, fitText(g, payloads_[idx], r.w - 160, fontM()).c_str(), fontM(),
         kTextHi, kLeft);
    pill(g, r.x + r.w - 110, r.y + 12, isPreset ? "preset" : "SD", fontS(), kBg,
         isPreset ? kAccent : kGreen);
  }
}

void WirelessOpsUi::drawLog_(Arduino_GFX *g) {
  uint8_t entries = log_ ? log_->count() : 0;
  uint8_t pages = logPageCount();
  text(g, kBodyX, 96, "ACTIVITY LOG", fontL(), kTextHi, kLeft);
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

  const int16_t rowH = 54;
  const int16_t rowGap = 6;
  const int16_t startY = 146;
  for (uint8_t k = 0; k < kLogRowsPerPage; ++k) {
    uint8_t idx = logPage_ * kLogRowsPerPage + k;
    ScanLog::Row row;
    if (!log_->rowFromNewest(idx, row)) break;
    int16_t y = startY + k * (rowH + rowGap);
    panel(g, kBodyX, y, kBodyW, rowH, 10, kSurface, 1, kLine);
    pill(g, kBodyX + 14, y + 14, log_->typeName(row.type), fontS(), kBg, logTypeColor(row.type));
    char ts[16];
    snprintf(ts, sizeof(ts), "t+%.1fs", row.timestampMs / 1000.0);
    text(g, kBodyX + kBodyW - 16, y + 10, ts, fontS(), kTextMut, kRight);
    int16_t tx = kBodyX + 110;
    text(g, tx, y + 8, fitText(g, row.summary, kBodyW - 260, fontM()).c_str(), fontM(), kTextHi,
         kLeft);
    text(g, tx, y + 32, fitText(g, row.detail, kBodyW - 130, fontS()).c_str(), fontS(), kTextMut,
         kLeft);
  }
}

#else  // ---------------- headless stubs ----------------

bool WirelessOpsUi::begin() { return false; }
void WirelessOpsUi::markDirty_() {}
WirelessEvent WirelessOpsUi::tick() { return WirelessEvent(); }
void WirelessOpsUi::showBusy(const String &) {}

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
