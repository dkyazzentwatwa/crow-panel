#ifndef CYPHERDRIVE_WIRELESS_OPS_UI_H
#define CYPHERDRIVE_WIRELESS_OPS_UI_H

#include <Arduino.h>

#include "../config/ProjectConfig.h"
#include "BleC6.h"
#include "HidPad.h"
#include "ScanLog.h"
#include "TouchKeyboard.h"
#include "WifiOps.h"
#include "WirelessTypes.h"
#include <TouchInput.h>

// Touch-first ACTIVE wireless + HID field console for the 1024x600 CrowPanel DSI
// panel. Four tabbed screens - WIFI / BLE / HID / LOG - drawn on
// CrowDisplay::canvas() with the shared Widgets toolkit. Rebuilt from the old
// passive console: this tool joins, interrogates, and emits.
//
// tick() services debounced touch and returns a typed WirelessEvent the .ino
// executes (scan / join / run a client tool / connect GATT / fire a HID slot /
// toggle output). Navigation (tab switch, row select, log paging) is
// presentational and handled internally. The UI never mutates app state; every
// touch path also has a serial command for headless parity.

enum WirelessScreen : uint8_t {
  WSCR_WIFI = 0,
  WSCR_BLE,
  WSCR_HID,
  WSCR_PAYLOAD,
  WSCR_LOG,
  WSCR_COUNT,
};

enum WirelessAction : uint8_t {
  WACT_NONE = 0,
  WACT_SCAN_WIFI,
  WACT_JOIN_WIFI,          // join the selected Wi-Fi row (index = row)
  WACT_LEAVE_WIFI,
  WACT_TOOL_CAPTIVE,       // captive-portal detection on the current link
  WACT_TOOL_MDNS,          // mDNS/service discovery
  WACT_TOOL_PORTSCAN,      // TCP connect sweep of the gateway
  WACT_TOOL_SWEEP,         // TCP host discovery across the gateway /24
  WACT_SCAN_BLE,
  WACT_CONNECT_BLE,        // connect+enumerate the selected BLE row (index = row)
  WACT_DISCONNECT_BLE,
  WACT_HID_SLOT,           // fire HID macro slot (index = slot)
  WACT_HID_OUTPUT_TOGGLE,  // switch USB <-> BLE
  WACT_SAVE_WIFI,          // export the selected Wi-Fi row to SD
  WACT_SAVE_BLE,           // export the selected BLE device to SD
  WACT_RUN_PAYLOAD,        // run payload list entry (index)
  WACT_STOP_PAYLOAD,       // stop the running payload
};

struct WirelessEvent {
  WirelessAction action = WACT_NONE;
  int8_t index = -1;
};

class WirelessOpsUi {
 public:
  static const uint8_t kMaxWifi = 24;
  static const uint8_t kMaxBle = 24;
  static const uint8_t kCardsPerPage = 6;  // 3 rows x 2, paged
  static const uint8_t kMaxServices = 6;
  static const uint8_t kMaxPorts = 10;
  static const uint8_t kMaxBleServices = 8;
  static const uint8_t kMaxPayloads = 20;
  static const uint8_t kLogRowsPerPage = 6;

  bool begin();

  // --- Data intake (the .ino pushes snapshots; the UI owns stable copies). ---
  void setWifi(const WifiNetworkRecord *rows, uint8_t count);
  void setLink(const WifiLinkStatus &link);
  void setCaptive(CaptivePortalResult result);
  void setServices(const ServiceRecord *rows, uint8_t count);
  void setPorts(const PortResult *rows, uint8_t count);
  void setBle(const BleDeviceRecord *rows, uint8_t count);
  void setBleServices(const BleServiceRecord *rows, uint8_t count, const String &addr);
  void setHid(const HidPad *hid) { hid_ = hid; }
  void setLog(const ScanLog *log);
  void setStatus(const String &status);
  void setPayloads(const String *names, uint8_t count, uint8_t presetCount);
  void setPayloadStatus(const String &name, uint8_t pct, bool running);

  // A results popup for the client tools (captive / mDNS / port-scan / sweep):
  // shows what the tool found, since there is no serial console on the panel.
  static const uint8_t kMaxToolLines = 12;
  void showToolResult(const String &title, const String *lines, uint8_t count);
  // Draw a "running <label>..." splash immediately (blocking tools take a few
  // seconds); call right before the blocking tool runs. No-op headless.
  void showBusy(const String &label);
  bool toolModalActive() const { return toolModal_; }

  // Password captured by the on-screen keyboard for the pending JOIN (empty if
  // the network is open or a config key should be used). Serial can pre-set it.
  const String &enteredPassword() const { return joinPassword_; }
  void setEnteredPassword(const String &pw) { joinPassword_ = pw; }
  void clearEnteredPassword() { joinPassword_ = ""; }
  bool keyboardActive() const { return kbdActive_; }

  // One tick: services touch, repaints when dirty, returns the launched event.
  WirelessEvent tick();

  // --- Serial-parity navigation. Return true when the request was valid. ---
  bool showScreen(const String &name);   // "wifi" | "ble" | "hid" | "log"
  bool selectNetwork(uint8_t index);     // select + open the Wi-Fi row detail
  bool selectBle(uint8_t index);         // select a BLE row
  void closeDetail();
  void pageLog(int8_t dir);
  void setLogPage(uint8_t page);

  // --- State getters (headless for selftest + the `touch` command). ---
  WirelessScreen screen() const { return screen_; }
  const char *screenName() const;
  bool detailOpen() const { return detail_; }
  uint8_t wifiSelected() const { return wifiSel_; }
  uint8_t bleSelected() const { return bleSel_; }
  uint8_t wifiCount() const { return wifiCount_; }
  uint8_t bleCount() const { return bleCount_; }
  uint8_t logPage() const { return logPage_; }
  uint8_t logPageCount() const;
  const char *bannerText() const { return "ACTIVE FIELD TOOL"; }

  void printTouch(Print &out) const;

 private:
  WirelessScreen screen_ = WSCR_WIFI;
  bool detail_ = false;
  uint8_t wifiSel_ = 0;
  uint8_t bleSel_ = 0;
  uint8_t wifiPage_ = 0;
  uint8_t blePage_ = 0;
  uint8_t logPage_ = 0;

  WifiNetworkRecord wifi_[kMaxWifi];
  uint8_t wifiCount_ = 0;
  WifiLinkStatus link_;
  CaptivePortalResult captive_ = CAPTIVE_UNKNOWN;
  ServiceRecord services_[kMaxServices];
  uint8_t serviceCount_ = 0;
  PortResult ports_[kMaxPorts];
  uint8_t portCount_ = 0;
  BleDeviceRecord ble_[kMaxBle];
  uint8_t bleCount_ = 0;
  BleServiceRecord bleServices_[kMaxBleServices];
  uint8_t bleServiceCount_ = 0;
  String bleConnectedAddr_;
  const HidPad *hid_ = nullptr;
  const ScanLog *log_ = nullptr;
  String status_ = "ready";

  // Payload list (presets first, then SD payloads) + running state.
  String payloads_[kMaxPayloads];
  uint8_t payloadCount_ = 0;
  uint8_t payloadPresetCount_ = 0;
  uint8_t payloadPage_ = 0;
  String payloadRunName_;
  uint8_t payloadPct_ = 0;
  bool payloadRunning_ = false;

  // On-screen keyboard modal for entering a Wi-Fi key on the panel.
  TouchKeyboard kbd_;
  bool kbdActive_ = false;
  String kbdBuffer_;
  String joinPassword_;

  // Client-tool results popup.
  bool toolModal_ = false;
  String toolTitle_;
  String toolLines_[kMaxToolLines];
  uint8_t toolLineCount_ = 0;

  CrowTouch touch_;  // shared debounced touch; a never-pressed stub headless.

  void clampLogPage_();
  void markDirty_();

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool ready_ = false;
  bool dirty_ = true;

  WirelessEvent handleRelease_(int16_t x, int16_t y);
  WirelessEvent handleKeyboard_(int16_t x, int16_t y);
  void draw_();
  void drawChrome_();
  void drawKeyboard_(class Arduino_GFX *g);
  void drawToolResult_(class Arduino_GFX *g);
  void drawWifi_(class Arduino_GFX *g);
  void drawBle_(class Arduino_GFX *g);
  void drawHid_(class Arduino_GFX *g);
  void drawPayload_(class Arduino_GFX *g);
  void drawLog_(class Arduino_GFX *g);
#endif
};

#endif  // CYPHERDRIVE_WIRELESS_OPS_UI_H
