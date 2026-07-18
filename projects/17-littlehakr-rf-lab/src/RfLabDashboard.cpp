#include "RfLabDashboard.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>

namespace {
constexpr uint16_t kBg = 0x0861;
constexpr uint16_t kSurface = 0x10A3;
constexpr uint16_t kText = 0xFFFF;
constexpr uint16_t kMuted = 0x9CF3;
constexpr uint16_t kGreen = 0x4FEA;
constexpr uint16_t kAmber = 0xFD40;
constexpr uint16_t kRed = 0xF945;
constexpr uint16_t kCyan = 0x05DF;
constexpr int16_t kHeaderH = 58;
constexpr int16_t kButtonY = 500;

const char *boolLabel(bool value) { return value ? "YES" : "NO"; }
}

void RfLabDashboard::begin() {
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "LittleHakr RF Lab") &&
           CrowDisplay::canvas() != nullptr;
  dirty_ = true;
}

void RfLabDashboard::setBanner(const String &banner) {
  banner_ = banner;
  dirty_ = true;
}

bool RfLabDashboard::tick(const RfLabState &lab, const C6RadioSnapshot &c6,
                          bool persistenceReady, RfLabUiEvent &event) {
  event.action = kRfLabUiNone;
  if (!ready_) return false;
  handleTouch_(event);
  if (dirty_ || millis() - lastDrawMs_ >= 1000UL) {
    draw_(lab, c6, persistenceReady);
    dirty_ = false;
    lastDrawMs_ = millis();
  }
  return event.action != kRfLabUiNone;
}

void RfLabDashboard::text_(int16_t x, int16_t y, uint8_t size, uint16_t color,
                            const String &value) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->setTextSize(size);
  g->setTextColor(color);
  g->setCursor(x, y);
  g->print(value);
}

void RfLabDashboard::card_(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t border) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillRoundRect(x, y, w, h, 10, kSurface);
  g->drawRoundRect(x, y, w, h, 10, border);
}

void RfLabDashboard::draw_(const RfLabState &lab, const C6RadioSnapshot &c6,
                            bool persistenceReady) {
  Arduino_GFX *g = CrowDisplay::canvas();
  g->fillScreen(kBg);
  text_(20, 16, 2, kText, "LITTLEHAKR RF LAB");
  const char *tabs[] = {"LAB", "C6 WIFI", "C6 BLE", "PROOF"};
  for (uint8_t i = 0; i < 4; ++i) {
    int16_t x = 410 + i * 150;
    uint16_t color = page_ == i ? kCyan : kMuted;
    g->drawRoundRect(x, 10, 136, 36, 8, color);
    text_(x + 12, 21, 1, color, tabs[i]);
  }
  g->drawFastHLine(0, kHeaderH, 1024, kMuted);
  if (page_ == 0) drawLab_(lab, persistenceReady);
  else if (page_ == 1) drawWifi_(c6);
  else if (page_ == 2) drawBle_(c6);
  else drawProof_();
  text_(20, 575, 1, kMuted, banner_);
}

void RfLabDashboard::drawLab_(const RfLabState &lab, bool persistenceReady) {
  card_(20, 82, 300, 150, lab.spiReady ? kGreen : kRed);
  text_(38, 101, 1, kMuted, "PROOF");
  text_(38, 129, 2, lab.spiReady ? kGreen : kRed, rfLabProofLabel(lab.proof));
  text_(38, 175, 1, kText, String("SPI ") + (lab.spiReady ? "READY" : "ERROR"));
  text_(38, 197, 1, kAmber, "TX DISABLED");

  card_(350, 82, 300, 150, lab.nrfDetected ? kGreen : kRed);
  text_(368, 101, 1, kMuted, "nRF24L01+");
  text_(368, 130, 2, lab.nrfDetected ? kGreen : kRed,
        lab.nrfDetected ? "DETECTED" : "MISSING");
  text_(368, 178, 1, kText, String("STATUS 0x") + String(lab.nrfStatus, HEX));
  text_(368, 198, 1, kMuted, String("RPD hits ") + lab.nrfActivityHits);

  card_(680, 82, 324, 150, lab.ccDetected ? kGreen : kRed);
  text_(698, 101, 1, kMuted, "CC1101 433 MHz");
  text_(698, 130, 2, lab.ccDetected ? kGreen : kRed,
        lab.ccDetected ? "DETECTED" : "MISSING");
  text_(698, 178, 1, kText, String("PART 0x") + String(lab.ccPartnum, HEX) +
                                  " VER 0x" + String(lab.ccVersion, HEX));
  text_(698, 198, 1, kMuted, String("RSSI ") + lab.ccRssiDbm + " dBm");

  card_(20, 260, 480, 190, kCyan);
  text_(38, 280, 1, kMuted, "RECEIVE-ONLY DETECTOR");
  text_(38, 312, 2, lab.detectorRunning ? kGreen : kAmber,
        lab.detectorRunning ? "RUNNING" : "PAUSED");
  text_(38, 354, 1, kText, String("profile ") + RF_LAB_PROFILE_NAME);
  text_(38, 378, 1, kText, String("authorized ") + boolLabel(lab.detectorAuthorized));
  text_(38, 402, 1, kMuted, String("nRF samples ") + lab.nrfSamples +
                                  "  CC samples " + lab.ccSamples);

  card_(524, 260, 480, 190, kCyan);
  text_(542, 280, 1, kMuted, "CC1101 GDO PROOF");
  text_(542, 316, 2, kText, String("GDO0 ") + (lab.gdo0High ? "HIGH" : "LOW") +
                              "  GDO2 " + (lab.gdo2High ? "HIGH" : "LOW"));
  text_(542, 360, 1, kText, String("transitions ") + lab.gdoTransitions);
  text_(542, 384, 1, kMuted, String("FFat ") + (persistenceReady ? "READY" : "RAM ONLY"));
  text_(542, 408, 1, kMuted, "No payloads, IDs, or raw traces stored");

  const char *labels[] = {"PROBE", lab.detectorRunning ? "PAUSE" : "START", "SAVE", "CLEAR"};
  const uint16_t colors[] = {kCyan, lab.detectorRunning ? kAmber : kGreen, kCyan, kRed};
  for (uint8_t i = 0; i < 4; ++i) {
    int16_t x = 20 + i * 251;
    card_(x, kButtonY, 232, 54, colors[i]);
    text_(x + 75, kButtonY + 20, 2, colors[i], labels[i]);
  }
}

void RfLabDashboard::drawWifi_(const C6RadioSnapshot &c6) {
  card_(20, 82, 984, 300, c6.wifiReady ? kGreen : kAmber);
  text_(44, 110, 2, kCyan, "ONBOARD ESP32-C6 WI-FI");
  text_(44, 160, 2, c6.wifiScanning ? kAmber : kText,
        c6.wifiScanning ? "PASSIVE SCAN RUNNING" : c6.wifiStatus);
  text_(44, 220, 2, kText, String("nearby AP count ") + c6.wifiNetworks);
  text_(44, 262, 2, kText, String("strongest relative RSSI ") + c6.wifiStrongestRssi + " dBm");
  text_(44, 304, 1, kMuted, "Aggregate only. This page does not show or store SSIDs, BSSIDs, or credentials.");
  text_(44, 330, 1, kMuted, String("scans ") + c6.wifiScans + "  C6 SDIO status " +
                                   (c6.wifiReady ? "READY" : "NOT READY"));
  card_(20, kButtonY, 480, 54, kCyan);
  text_(165, kButtonY + 20, 2, kCyan, "REFRESH WI-FI");
}

void RfLabDashboard::drawBle_(const C6RadioSnapshot &c6) {
  card_(20, 82, 984, 300, c6.bleAvailable ? kGreen : kAmber);
  text_(44, 110, 2, kCyan, "ONBOARD ESP32-C6 BLUETOOTH LE");
  text_(44, 164, 2, c6.bleAvailable ? kGreen : kAmber, c6.bleStatus);
  text_(44, 220, 2, kText, String("aggregate reports ") + c6.bleReports);
  text_(44, 278, 1, kMuted, "This page is separate from RF Lab and retains no addresses, names, or advertisements.");
  text_(44, 304, 1, kMuted, "Current P4 Arduino profile must expose C6 hosted NimBLE before BLE scans can run.");
  text_(44, 330, 1, kMuted, "Until then it is an honest firmware-gated status page, not a mock scan.");
}

void RfLabDashboard::drawProof_() {
  card_(20, 82, 984, 360, kAmber);
  text_(44, 110, 2, kAmber, "PROOF BOUNDARY");
  text_(44, 168, 2, kText, "PROVEN BY THIS FIRMWARE");
  text_(44, 208, 1, kText, "Boot, screen, touch, SPI bus, radio register reads, GDO input levels,");
  text_(44, 232, 1, kText, "fixed receive-only activity counters, and optional aggregate FFat summary.");
  text_(44, 292, 2, kRed, "NOT IMPLEMENTED");
  text_(44, 332, 1, kText, "TX, payload reads, protocol decoding, device IDs, replay, jamming, brute force,");
  text_(44, 356, 1, kText, "generic channel exploration, raw traces, or credential collection.");
  text_(44, 410, 1, kMuted, "Compile success is not live-radio proof. Test only your authorized lab equipment.");
}

void RfLabDashboard::handleTouch_(RfLabUiEvent &event) {
  int16_t x = 0, y = 0;
  bool touched = CrowDisplay::touchPoint(x, y);
  if (!touched || wasTouched_ || millis() - lastTouchMs_ < 120UL) {
    wasTouched_ = touched;
    return;
  }
  lastTouchMs_ = millis();
  wasTouched_ = true;
  if (y < kHeaderH) {
    if (x >= 410 && x < 560) page_ = 0;
    else if (x < 710) page_ = 1;
    else if (x < 860) page_ = 2;
    else page_ = 3;
    dirty_ = true;
    return;
  }
  if (page_ == 0 && y >= kButtonY) {
    int index = (x - 20) / 251;
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    const RfLabUiAction actions[] = {kRfLabUiProbe, kRfLabUiDetectorToggle,
                                     kRfLabUiSave, kRfLabUiClear};
    event.action = actions[index];
  } else if (page_ == 1 && y >= kButtonY && x < 500) {
    event.action = kRfLabUiWifiScan;
  }
}
#else
void RfLabDashboard::begin() {}
void RfLabDashboard::setBanner(const String &banner) { banner_ = banner; }
bool RfLabDashboard::tick(const RfLabState &, const C6RadioSnapshot &, bool, RfLabUiEvent &event) {
  event.action = kRfLabUiNone;
  return false;
}
#endif
