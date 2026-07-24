#include "StarbeamUi.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#include <DashboardWidgets.h>

using namespace Widgets;

namespace {
// Starbeam identity accent on top of the shared "ops" palette.
constexpr uint16_t kMagenta = rgb(0xE0, 0x4C, 0xC8);
constexpr uint16_t kCyan = kAccent;

constexpr int16_t kW = 1024, kH = 600;
constexpr int16_t kHeaderH = 56;
constexpr int16_t kBannerY = 572;

struct Rect { int16_t x, y, w, h; };
bool inRect(const Rect &r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// --- Home category cards (3x2) ---
Rect homeCard(uint8_t i) {
  int col = i % 3, row = i / 3;
  return {int16_t(16 + col * 336), int16_t(72 + row * 246), 320, 230};
}

// --- Category action tiles (3 columns) ---
Rect catTile(uint8_t idx) {
  int col = idx % 3, row = idx / 3;
  return {int16_t(16 + col * 336), int16_t(120 + row * 102), 320, 88};
}

const Rect kBackBtn = {16, 496, 200, 52};
const Rect kStopBtn = {800, 496, 208, 52};
const Rect kLegalCard = {212, 120, 600, 360};
const Rect kAckBtn = {262, 400, 240, 56};
const Rect kCancelBtn = {522, 400, 240, 56};

// Collect the actions belonging to a category, preserving table order.
uint8_t collectCategory(StarbeamCategory c, StarbeamAction out[16]) {
  uint8_t n = 0;
  for (uint8_t a = 1; a < ACT_COUNT && n < 16; ++a) {
    if (kStarbeamActions[a].category == c) out[n++] = (StarbeamAction)a;
  }
  return n;
}

uint16_t signalColor(uint8_t level, uint8_t maxLevel) {
  if (maxLevel == 0) return kGreen;
  float f = (float)level / maxLevel;
  if (f > 0.66f) return kMagenta;
  if (f > 0.33f) return kAmber;
  return kGreen;
}
}  // namespace

bool StarbeamUi::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "STARBEAM CONSOLE") &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
  return ready_;
}

void StarbeamUi::setBanner(const char *) { dirty_ = true; }

void StarbeamUi::toHome() {
  screen_ = SCR_HOME;
  dirty_ = true;
}

void StarbeamUi::showOperation(StarbeamAction a) {
  (void)a;
  screen_ = SCR_OPERATION;
  dirty_ = true;
}

StarbeamAction StarbeamUi::tick(StarbeamState &st) {
  if (!ready_) return ACT_NONE;
  StarbeamAction launched = handleTouch_(st);
  // Operation screens animate (spectrum/scan/telemetry); redraw at ~10 Hz.
  bool animate = screen_ == SCR_OPERATION;
  if (dirty_ || (animate && millis() - lastDrawMs_ >= 100) ||
      (!animate && millis() - lastDrawMs_ >= 1000)) {
    draw_(st);
    dirty_ = false;
    lastDrawMs_ = millis();
  }
  return launched;
}

StarbeamAction StarbeamUi::handleTouch_(StarbeamState &st) {
  int16_t x = 0, y = 0;
  bool touched = CrowDisplay::touchPoint(x, y);
  if (!touched || wasTouched_ || millis() - lastTouchMs_ < 140) {
    wasTouched_ = touched;
    return ACT_NONE;
  }
  lastTouchMs_ = millis();
  wasTouched_ = true;
  dirty_ = true;

  switch (screen_) {
    case SCR_HOME: {
      for (uint8_t i = 0; i < CAT_COUNT; ++i) {
        if (inRect(homeCard(i), x, y)) {
          category_ = (StarbeamCategory)i;
          screen_ = SCR_CATEGORY;
          return ACT_NONE;
        }
      }
      return ACT_NONE;
    }
    case SCR_CATEGORY: {
      if (inRect(kBackBtn, x, y)) { screen_ = SCR_HOME; return ACT_NONE; }
      StarbeamAction acts[16];
      uint8_t n = collectCategory(category_, acts);
      for (uint8_t i = 0; i < n; ++i) {
        if (inRect(catTile(i), x, y)) {
          const StarbeamActionInfo &info = starbeamAction(acts[i]);
          if (info.requiresTx) { pending_ = acts[i]; screen_ = SCR_LEGAL; return ACT_NONE; }
          screen_ = SCR_OPERATION;
          return acts[i];
        }
      }
      return ACT_NONE;
    }
    case SCR_LEGAL: {
      if (inRect(kAckBtn, x, y)) {
        StarbeamAction a = pending_;
        pending_ = ACT_NONE;
        screen_ = SCR_OPERATION;
        return a;
      }
      if (inRect(kCancelBtn, x, y)) { pending_ = ACT_NONE; screen_ = SCR_CATEGORY; }
      return ACT_NONE;
    }
    case SCR_OPERATION: {
      if (inRect(kStopBtn, x, y)) { screen_ = SCR_CATEGORY; return ACT_STOP_ALL; }
      if (inRect(kBackBtn, x, y)) { screen_ = SCR_CATEGORY; return ACT_STOP_ALL; }
      return ACT_NONE;
    }
  }
  return ACT_NONE;
}

// --------------------------------------------------------------------------
// Drawing
// --------------------------------------------------------------------------

void StarbeamUi::draw_(StarbeamState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillScreen(kBg);
  drawHeader_(st);
  switch (screen_) {
    case SCR_HOME: drawHome_(); break;
    case SCR_CATEGORY: drawCategory_(st); break;
    case SCR_OPERATION: drawOperation_(st); break;
    case SCR_LEGAL: drawCategory_(st); drawLegal_(); break;
  }
  text(g, 16, kBannerY, st.banner, fontS(), kTextMut, kLeft);
}

void StarbeamUi::drawHeader_(StarbeamState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();
  text(g, 16, 14, "STARBEAM", fontL(), kTextHi, kLeft);
  int16_t wm = textWidth(g, "STARBEAM", fontL());
  text(g, 16 + wm + 8, 18, "CONSOLE", fontS(), kMagenta, kLeft);

  // 7 radio detect dots (5 nRF + 2 CC), centre-right.
  int16_t dx = 470;
  text(g, dx - 34, 20, "NRF", fontS(), kTextMut, kLeft);
  for (uint8_t i = 0; i < 5; ++i) {
    statusDot(g, dx + i * 26, kHeaderH / 2, 7, st.nrf[i].present ? kGreen : kRed);
  }
  int16_t cx = dx + 5 * 26 + 18;
  text(g, cx - 30, 20, "CC", fontS(), kTextMut, kLeft);
  for (uint8_t i = 0; i < 2; ++i) {
    statusDot(g, cx + i * 26, kHeaderH / 2, 7, st.cc[i].present ? kGreen : kRed);
  }

  // Co-processor link pill + TX gate, right-aligned.
  const char *link = st.co.linked ? "UART LINK" : "UART OFFLINE";
  uint16_t linkFill = st.co.linked ? kGreen : kLine;
  uint16_t linkText = st.co.linked ? kBg : kTextMut;
  int16_t pw = textWidth(g, link, fontS()) + 24;
  pill(g, kW - 16 - pw, 12, link, fontS(), linkText, linkFill);
  const char *tx = st.txConfirmed ? "TX ARMED" : "TX SAFE";
  int16_t tw = textWidth(g, tx, fontS()) + 24;
  pill(g, kW - 16 - pw - 8 - tw, 12, tx, fontS(), st.txConfirmed ? kBg : kTextMut,
       st.txConfirmed ? kAmber : kLine);

  g->drawFastHLine(0, kHeaderH, kW, kLine);
}

void StarbeamUi::drawHome_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  const char *icons = "";
  (void)icons;
  for (uint8_t i = 0; i < CAT_COUNT; ++i) {
    Rect r = homeCard(i);
    panel(g, r.x, r.y, r.w, r.h, 16, kSurface, 1, kLine);
    towerIcon(g, r.x + 24, r.y + 24, i == CAT_SECURITY ? kMagenta : kCyan);
    text(g, r.x + 24, r.y + r.h - 96, starbeamCategoryLabel((StarbeamCategory)i),
         fontL(), kTextHi, kLeft);
    StarbeamAction acts[16];
    uint8_t n = collectCategory((StarbeamCategory)i, acts);
    char sub[24];
    snprintf(sub, sizeof(sub), "%u actions", n);
    text(g, r.x + 24, r.y + r.h - 56, sub, fontS(), kTextMut, kLeft);
    text(g, r.x + r.w - 24, r.y + r.h - 52, "OPEN >", fontS(), kCyan, kRight);
  }
}

void StarbeamUi::drawCategory_(StarbeamState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();
  text(g, 16, 76, starbeamCategoryLabel(category_), fontL(), kMagenta, kLeft);
  StarbeamAction acts[16];
  uint8_t n = collectCategory(category_, acts);
  for (uint8_t i = 0; i < n; ++i) {
    Rect r = catTile(i);
    const StarbeamActionInfo &info = starbeamAction(acts[i]);
    bool active = st.running && st.active == acts[i];
    panel(g, r.x, r.y, r.w, r.h, 12, active ? kSurfaceHi : kSurface, 1,
          active ? kGreen : kLine);
    text(g, r.x + 18, r.y + 20, info.label, fontM(), kTextHi, kLeft);
    const char *tag = info.target == TGT_COPROC ? "UART" :
                      info.target == TGT_NATIVE ? "RADIO" : "PANEL";
    text(g, r.x + 18, r.y + 54, tag, fontS(), kTextMut, kLeft);
    if (info.requiresTx) {
      pill(g, r.x + r.w - 66, r.y + 50, "TX", fontS(), kBg, kAmber);
    }
    if (active) statusDot(g, r.x + r.w - 22, r.y + 24, 6, kGreen);
  }
  panel(g, kBackBtn.x, kBackBtn.y, kBackBtn.w, kBackBtn.h, 10, kSurface, 1, kLine);
  text(g, kBackBtn.x + kBackBtn.w / 2, kBackBtn.y + 16, "< HOME", fontM(), kTextHi, kCenter);
}

void StarbeamUi::drawSpectrum_(StarbeamState &st, int x, int y, int w, int h) {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, x, y, w, h, 12, kSurface, 1, kLine);
  int plotX = x + 12, plotY = y + 12, plotW = w - 24, plotH = h - 40;
  float barW = (float)plotW / 128.0f;
  for (int ch = 0; ch < 128; ++ch) {
    uint8_t lvl = st.spectrum[ch];
    int bh = (int)((lvl / 10.0f) * plotH);
    int bx = plotX + (int)(ch * barW);
    g->fillRect(bx, plotY + plotH - bh, (int)barW > 1 ? (int)barW - 1 : 1, bh,
                signalColor(lvl, 10));
  }
  // Wi-Fi channel markers (2412/2437/2462 MHz -> nRF ch 12/37/62).
  const int wch[3] = {12, 37, 62};
  const char *wl[3] = {"1", "6", "11"};
  for (int i = 0; i < 3; ++i) {
    int mx = plotX + (int)(wch[i] * barW);
    g->drawFastVLine(mx, plotY, plotH, kLine);
    text(g, mx, plotY + plotH + 6, wl[i], fontS(), kTextMut, kCenter);
  }
}

void StarbeamUi::drawHeatmap_(StarbeamState &st, int x, int y, int w, int h) {
  Arduino_GFX *g = CrowDisplay::canvas();
  panel(g, x, y, w, h, 12, kSurface, 1, kLine);
  int rows = 14;
  int rowH = (h - 40) / rows;
  for (int ch = 0; ch < rows; ++ch) {
    int ry = y + 20 + ch * rowH;
    char lbl[6];
    snprintf(lbl, sizeof(lbl), "%d", ch + 1);
    text(g, x + 16, ry + 2, lbl, fontS(), kTextMut, kLeft);
    hBar(g, x + 50, ry, w - 70, rowH - 6, st.co.wifiChannels[ch] / 100.0f,
         st.co.wifiChannels[ch] > 60 ? kMagenta : kCyan);
  }
}

void StarbeamUi::drawOperation_(StarbeamState &st) {
  Arduino_GFX *g = CrowDisplay::canvas();
  const StarbeamActionInfo &info = starbeamAction(st.active);
  text(g, 16, 72, info.label, fontL(), kTextHi, kLeft);
  const char *state = st.running ? "ACTIVE" : "IDLE";
  uint16_t stateColor = st.running ? kGreen : kTextMut;
  if (info.requiresTx && !st.txConfirmed) { state = "TX DISARMED"; stateColor = kAmber; }
  text(g, kW - 16, 74, state, fontL(), stateColor, kRight);

  const int cx = 16, cy = 112, cw = kW - 32, ch = 372;

  switch (st.active) {
    case ACT_NRF_SCAN:
      drawSpectrum_(st, cx, cy, cw, ch);
      break;
    case ACT_WIFI_HEATMAP:
      drawHeatmap_(st, cx, cy, cw, ch);
      break;
    case ACT_CC_SCAN:
    case ACT_GET_RSSI: {
      panel(g, cx, cy, 360, ch, 12, kSurface, 1, kLine);
      int gx = cx + 180, gy = cy + 170;
      float v = (st.ccRssiDbm + 120.0f) / 120.0f;
      arcGauge(g, gx, gy, 120, 92, constrain(v, 0.0f, 1.0f), kCyan);
      char rssi[16];
      snprintf(rssi, sizeof(rssi), "%d", (int)st.ccRssiDbm);
      text(g, gx, gy - 20, rssi, fontXL(), kTextHi, kCenter);
      text(g, gx, gy + 24, "dBm", fontS(), kTextMut, kCenter);
      panel(g, cx + 380, cy, cw - 380, ch, 12, kSurface, 1, kLine);
      char f[24];
      snprintf(f, sizeof(f), "%.2f MHz", st.ccFreqMhz);
      text(g, cx + 404, cy + 30, "FREQUENCY", fontS(), kTextMut, kLeft);
      text(g, cx + 404, cy + 54, f, fontL(), kTextHi, kLeft);
      char lqi[24];
      snprintf(lqi, sizeof(lqi), "LQI %d", st.ccLqi);
      text(g, cx + 404, cy + 120, lqi, fontM(), kTextHi, kLeft);
      break;
    }
    case ACT_WIFI_SCAN:
    case ACT_BLE_SCAN: {
      panel(g, cx, cy, cw, ch, 12, kSurface, 1, kLine);
      bool ble = st.active == ACT_BLE_SCAN;
      text(g, cx + 24, cy + 30, ble ? "BLE DEVICES" : "WIFI NETWORKS", fontS(), kTextMut, kLeft);
      char n[16];
      snprintf(n, sizeof(n), "%u", ble ? st.co.bleDevices : st.co.wifiNetworks);
      text(g, cx + 24, cy + 60, n, fontXL(), kTextHi, kLeft);
      if (!ble) {
        char rs[24];
        snprintf(rs, sizeof(rs), "strongest %d dBm", st.co.wifiStrongest);
        text(g, cx + 24, cy + 150, rs, fontM(), kTextHi, kLeft);
      }
      text(g, cx + 24, cy + ch - 40, st.co.status, fontS(), kTextMut, kLeft);
      break;
    }
    case ACT_DEAUTH_TARGET:
    case ACT_DEAUTH_ALL:
    case ACT_BEACON_FLOOD:
    case ACT_PROBE_FLOOD:
    case ACT_PMKID:
    case ACT_CAPTIVE_PORTAL: {
      panel(g, cx, cy, 490, ch, 12, kSurface, 1, kLine);
      text(g, cx + 24, cy + 30, "FRAMES SENT", fontS(), kTextMut, kLeft);
      char fr[16]; snprintf(fr, sizeof(fr), "%lu", (unsigned long)st.co.frames);
      text(g, cx + 24, cy + 58, fr, fontXL(), kMagenta, kLeft);
      text(g, cx + 24, cy + 150, "CLIENTS", fontS(), kTextMut, kLeft);
      char cl[16]; snprintf(cl, sizeof(cl), "%lu", (unsigned long)st.co.clients);
      text(g, cx + 24, cy + 178, cl, fontL(), kTextHi, kLeft);
      text(g, cx + 250, cy + 150, "CAPTURES", fontS(), kTextMut, kLeft);
      char cp[16]; snprintf(cp, sizeof(cp), "%lu", (unsigned long)st.co.captures);
      text(g, cx + 250, cy + 178, cp, fontL(), kTextHi, kLeft);
      panel(g, cx + 506, cy, cw - 506, ch, 12, kSurface, 1, kLine);
      text(g, cx + 530, cy + 30, "CO-PROCESSOR", fontS(), kTextMut, kLeft);
      text(g, cx + 530, cy + 58, st.co.linked ? "LINKED" : "OFFLINE", fontL(),
           st.co.linked ? kGreen : kRed, kLeft);
      text(g, cx + 530, cy + 120, st.co.status, fontS(), kTextMut, kLeft);
      text(g, cx + 530, cy + ch - 40, "Runs on UART ESP32 module", fontS(), kTextMut, kLeft);
      break;
    }
    case ACT_REC_RAW:
    case ACT_PLAY_RAW:
    case ACT_SHOW_RAW:
    case ACT_SHOW_BUFF: {
      panel(g, cx, cy, cw, ch, 12, kSurface, 1, kLine);
      char b[32];
      snprintf(b, sizeof(b), "%u bytes buffered", st.recBytes);
      text(g, cx + 24, cy + 30, "433 MHz RAW BUFFER", fontS(), kTextMut, kLeft);
      text(g, cx + 24, cy + 58, b, fontL(), kTextHi, kLeft);
      // hex preview grid rendered by the engine into banner; simple note here
      text(g, cx + 24, cy + ch - 40,
           st.recBufferValid ? "buffer valid - see serial for hex dump"
                             : "buffer empty",
           fontS(), kTextMut, kLeft);
      break;
    }
    default: {
      // jammers, tests, settings, help, freq presets, stop
      panel(g, cx, cy, cw, ch, 12, kSurface, 1, kLine);
      if (info.category == CAT_JAMMERS || st.active == ACT_CC1_SINGLE ||
          st.active == ACT_CC2_SINGLE) {
        char jc[24];
        snprintf(jc, sizeof(jc), "%lu", (unsigned long)st.jamCycles);
        text(g, cx + 24, cy + 30, "JAM CYCLES", fontS(), kTextMut, kLeft);
        text(g, cx + 24, cy + 58, jc, fontXL(), kMagenta, kLeft);
        char rc[32];
        snprintf(rc, sizeof(rc), "%u nRF + %u CC1101 active",
                 st.nrfPresentCount, st.ccPresentCount);
        text(g, cx + 24, cy + 160, rc, fontM(), kTextHi, kLeft);
        if (!st.txConfirmed)
          text(g, cx + 24, cy + 200, "Transmit disarmed - confirm LabProfile.h",
               fontS(), kAmber, kLeft);
      } else if (info.category == CAT_RADIOS) {
        text(g, cx + 24, cy + 30, "REGISTER PROOF", fontS(), kTextMut, kLeft);
        for (uint8_t i = 0; i < 5; ++i) {
          char l[40];
          snprintf(l, sizeof(l), "nRF%u  STATUS 0x%02X  %s", i, st.nrf[i].reg,
                   st.nrf[i].present ? "OK" : "--");
          text(g, cx + 24, cy + 64 + i * 26, l, fontS(),
               st.nrf[i].present ? kTextHi : kTextMut, kLeft);
        }
        for (uint8_t i = 0; i < 2; ++i) {
          char l[40];
          snprintf(l, sizeof(l), "CC%u   PARTNUM 0x%02X  %s", i, st.cc[i].reg,
                   st.cc[i].present ? "OK" : "--");
          text(g, cx + 360, cy + 64 + i * 26, l, fontS(),
               st.cc[i].present ? kTextHi : kTextMut, kLeft);
        }
      } else {
        text(g, cx + 24, cy + 30, info.label, fontL(), kTextHi, kLeft);
        text(g, cx + 24, cy + 90, st.banner, fontM(), kTextMut, kLeft);
      }
      break;
    }
  }

  // BACK + STOP controls
  panel(g, kBackBtn.x, kBackBtn.y, kBackBtn.w, kBackBtn.h, 10, kSurface, 1, kLine);
  text(g, kBackBtn.x + kBackBtn.w / 2, kBackBtn.y + 16, "< BACK", fontM(), kTextHi, kCenter);
  panel(g, kStopBtn.x, kStopBtn.y, kStopBtn.w, kStopBtn.h, 10, kRed, 0, kRed);
  text(g, kStopBtn.x + kStopBtn.w / 2, kStopBtn.y + 16, "STOP", fontL(), kBg, kCenter);
}

void StarbeamUi::drawLegal_() {
  Arduino_GFX *g = CrowDisplay::canvas();
  // solid backdrop over the content area so the modal reads cleanly
  g->fillRect(0, kHeaderH + 1, kW, kH - kHeaderH - 1, kBg);
  panel(g, kLegalCard.x, kLegalCard.y, kLegalCard.w, kLegalCard.h, 16, kSurfaceHi, 2, kAmber);
  text(g, kW / 2, kLegalCard.y + 28, "AUTHORIZED USE ONLY", fontL(), kAmber, kCenter);
  const char *lines[] = {
      "This action transmits RF / injects frames.",
      "Only operate on radios, frequencies, and",
      "networks you own or are explicitly",
      "authorized to test. You are responsible",
      "for legal compliance (FCC / CFAA / local law).",
  };
  for (int i = 0; i < 5; ++i)
    text(g, kLegalCard.x + 40, kLegalCard.y + 90 + i * 34, lines[i], fontM(), kTextHi, kLeft);
  panel(g, kAckBtn.x, kAckBtn.y, kAckBtn.w, kAckBtn.h, 10, kGreen, 0, kGreen);
  text(g, kAckBtn.x + kAckBtn.w / 2, kAckBtn.y + 18, "ACKNOWLEDGE", fontM(), kBg, kCenter);
  panel(g, kCancelBtn.x, kCancelBtn.y, kCancelBtn.w, kCancelBtn.h, 10, kSurface, 1, kLine);
  text(g, kCancelBtn.x + kCancelBtn.w / 2, kCancelBtn.y + 18, "CANCEL", fontM(), kTextHi, kCenter);
}

bool StarbeamUi::stopHit_(int, int) { return false; }

#else  // ---------------- non-display stubs ----------------

bool StarbeamUi::begin() { return false; }
void StarbeamUi::setBanner(const char *) {}
void StarbeamUi::toHome() { screen_ = SCR_HOME; }
void StarbeamUi::showOperation(StarbeamAction) { screen_ = SCR_OPERATION; }
StarbeamAction StarbeamUi::tick(StarbeamState &) { return ACT_NONE; }

#endif  // USE_DISPLAY && P4
