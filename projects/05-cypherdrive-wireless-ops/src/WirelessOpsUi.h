#ifndef CYPHERDRIVE_WIRELESS_OPS_UI_H
#define CYPHERDRIVE_WIRELESS_OPS_UI_H

#include <Arduino.h>

#include "../config/ProjectConfig.h"
#include "ScanLog.h"
#include "WirelessTypes.h"
#include <TouchInput.h>

// Touch-first passive wireless console for the 1024x600 CrowPanel DSI panel.
// Four tabbed screens - WIFI / BLE / LOG / QR - drawn on CrowDisplay::canvas()
// with the shared Widgets toolkit (dark "ops" palette, FreeSans fonts,
// cards / signal bars / pills). Replaces the generic serial-only ops dashboard.
//
// A persistent "PASSIVE - RECEIVE ONLY" banner sits in the header chrome on
// every screen: this is deliberately a look-don't-touch tool - no joining, no
// injection, no capture.
//
// tick() services debounced touch (CrowTouch) and returns a typed action the
// .ino executes (run a mock/real scan). Navigation - tab switches, opening the
// Wi-Fi inspector, paging the log - is presentational and handled internally.
// The UI never mutates application state itself. Every touch path also has a
// serial command (screen / net / page / scan) for headless parity.

enum WirelessScreen : uint8_t {
  WSCR_WIFI = 0,
  WSCR_BLE,
  WSCR_LOG,
  WSCR_QR,
  WSCR_COUNT,
};

enum WirelessAction : uint8_t {
  WACT_NONE = 0,
  WACT_SCAN_WIFI,
  WACT_SCAN_BLE,
};

class WirelessOpsUi {
 public:
  static const uint8_t kMaxWifi = 8;
  static const uint8_t kMaxBle = 8;
  static const uint8_t kLogRowsPerPage = 6;

  bool begin();

  // --- Data intake (called by the .ino after each mock/real update). Copies
  // rows so the UI owns a stable snapshot to paint from. ---
  void setWifi(const WifiNetworkRecord *rows, uint8_t count);
  void setBle(const BleAdvertisementRecord *rows, uint8_t count);
  void setLog(const ScanLog *log);
  void setQr(const String &url, bool persisted, const String &defaultUrl);
  void setStatus(const String &status);

  // One tick: services touch and repaints when dirty. Returns the action the
  // user launched this frame (WACT_NONE most frames).
  WirelessAction tick();

  // --- Serial-parity navigation (mirror the touch tab bar / row taps / paging).
  // Return true when the request was valid. ---
  bool showScreen(const String &name);   // "wifi" | "ble" | "log" | "qr"
  bool selectNetwork(uint8_t index);     // open the Wi-Fi inspector on a row
  void closeDetail();
  void pageLog(int8_t dir);              // -1 prev, +1 next, 0 clamp/refresh
  void setLogPage(uint8_t page);         // absolute page (clamped)

  // --- State getters (available headless for selftest + the `touch` command). ---
  WirelessScreen screen() const { return screen_; }
  const char *screenName() const;
  bool detailOpen() const { return detail_; }
  uint8_t wifiSelected() const { return wifiSel_; }
  uint8_t wifiCount() const { return wifiCount_; }
  uint8_t bleCount() const { return bleCount_; }
  uint8_t logPage() const { return logPage_; }
  uint8_t logPageCount() const;
  const char *bannerText() const { return "PASSIVE - RECEIVE ONLY"; }

  void printTouch(Print &out) const;  // `touch` command output

 private:
  // Plain navigation/model state - present in both builds so selftest and the
  // serial commands work with no panel attached.
  WirelessScreen screen_ = WSCR_WIFI;
  bool detail_ = false;
  uint8_t wifiSel_ = 0;
  uint8_t logPage_ = 0;

  WifiNetworkRecord wifi_[kMaxWifi];
  uint8_t wifiCount_ = 0;
  BleAdvertisementRecord ble_[kMaxBle];
  uint8_t bleCount_ = 0;
  const ScanLog *log_ = nullptr;
  String qrUrl_ = "";
  String qrDefault_ = "";
  bool qrPersisted_ = false;
  String status_ = "ready";

  CrowTouch touch_;  // shared debounced touch; a never-pressed stub headless.

  void clampLogPage_();
  void markDirty_();

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool ready_ = false;
  bool dirty_ = true;

  WirelessAction handleRelease_(int16_t x, int16_t y);
  void draw_();
  void drawChrome_();
  void drawWifiList_(class Arduino_GFX *g);
  void drawWifiDetail_(class Arduino_GFX *g);
  void drawBle_(class Arduino_GFX *g);
  void drawLog_(class Arduino_GFX *g);
  void drawQr_(class Arduino_GFX *g);
#endif
};

#endif  // CYPHERDRIVE_WIRELESS_OPS_UI_H
