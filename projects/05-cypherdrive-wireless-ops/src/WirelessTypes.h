#ifndef CYPHERDRIVE_WIRELESS_TYPES_H
#define CYPHERDRIVE_WIRELESS_TYPES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// One scanned Wi-Fi network. The active field tool keeps the passive fields
// (ssid/rssi/channel/auth/hidden) and adds BSSID so a specific AP can be
// targeted for a join or a client-tool run.
struct WifiNetworkRecord {
  String ssid;
  String bssid;
  int32_t rssi = 0;
  uint8_t channel = 0;
  String auth;
  bool hidden = false;
  String phy;        // best-effort 802.11 mode: b / g / n / ax
  bool wps = false;  // WPS advertised

  // 2.4 vs 5 GHz, derived from the channel (>=32 is 5 GHz on this radio).
  const char *band() const { return channel >= 32 ? "5GHz" : "2.4GHz"; }
  // A network with no PSK-style auth is joinable with no credentials.
  bool open() const { return auth == "open"; }
};

// Live Wi-Fi association state, surfaced on the WIFI screen once a join runs.
enum WifiLinkState : uint8_t {
  WLINK_IDLE = 0,     // not attempting
  WLINK_JOINING,      // association in progress
  WLINK_CONNECTED,    // got an IP
  WLINK_FAILED,       // gave up / auth failed
};

struct WifiLinkStatus {
  WifiLinkState state = WLINK_IDLE;
  String ssid;
  String ip;
  String gateway;
  int32_t rssi = 0;
  const char *stateName() const {
    switch (state) {
      case WLINK_JOINING: return "joining";
      case WLINK_CONNECTED: return "connected";
      case WLINK_FAILED: return "failed";
      default: return "idle";
    }
  }
};

// Captive-portal DETECTION only (never credential capture): a probe of a known
// no-content endpoint. A redirect / non-204 answer means a portal is in the way.
enum CaptivePortalResult : uint8_t {
  CAPTIVE_UNKNOWN = 0,  // not checked yet
  CAPTIVE_CLEAR,        // 204: open internet, no portal
  CAPTIVE_PORTAL,       // redirected/blocked: a portal is intercepting
  CAPTIVE_OFFLINE,      // probe could not run (no link)
};

// One service found by mDNS/service discovery on the joined LAN.
struct ServiceRecord {
  String name;
  String type;   // e.g. "_http._tcp"
  String host;
  String ip;
  uint16_t port = 0;
};

// One port from a TCP connect sweep of an authorized target host.
struct PortResult {
  uint16_t port = 0;
  bool open = false;
  const char *service = "";  // best-guess label for the port
};

// One BLE device seen by the on-panel C6 central scan (replaces the old
// UART-sidecar advertisement record). `connectable` gates the GATT detail view.
struct BleDeviceRecord {
  String name;
  String address;
  int32_t rssi = 0;
  String vendor;      // resolved from the BLE manufacturer company id
  String detail;      // first advertised service UUID, if any
  bool connectable = false;
  int16_t txPower = 0;   // advertised TX power (0 if not present)
  String addrType;       // public / random
};

// One GATT service enumerated after connecting to a BLE device.
struct BleServiceRecord {
  String uuid;
  String label;        // friendly name for well-known UUIDs
  uint8_t charCount = 0;
};

// Backwards-compatible alias: older log/record call sites used this name.
typedef BleDeviceRecord BleAdvertisementRecord;

#endif
