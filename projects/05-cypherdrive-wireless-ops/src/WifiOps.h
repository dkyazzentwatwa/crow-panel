#ifndef CYPHERDRIVE_WIFI_OPS_H
#define CYPHERDRIVE_WIFI_OPS_H

#include "WirelessTypes.h"

// Active Wi-Fi operations through the onboard ESP32-C6 (hosted esp_hosted link).
// Replaces the passive-only WifiScanner: an ACTIVE probe scan, joining a
// selected network, and a small set of client-side tools once associated
// (captive-portal DETECTION, mDNS/service discovery, and a TCP connect sweep of
// an authorized target). Mock-first: with USE_WIFI_ACTIVE=0 every call returns
// believable demo data so the whole tool is exercisable with no radio.
//
// Deliberately NOT here: deauth, jamming, evil-twin/captive-portal credential
// capture, or packet injection. Captive-portal handling is detection only.
class WifiOps {
 public:
  void begin();

  // Active probe scan (broadcasts probe requests; hears hidden SSIDs that a
  // passive scan misses). Fills records newest-strongest-first, returns count.
  uint8_t scan(WifiNetworkRecord records[], uint8_t maxRecords, Stream &out);

  const char *driverName() const;
  bool hardwareEnabled() const;

  // --- Association ---
  // Start joining a network. Non-blocking: poll maintain() and read status().
  // password may be nullptr/empty for an open network.
  bool join(const String &ssid, const char *password, Stream &out);
  void leave(Stream &out);
  void maintain();  // pump the async connect state machine; call each loop()
  const WifiLinkStatus &status() const { return link_; }

  // --- Client tools (need an active link on real builds; mock returns demo) ---
  // Probe a known no-content endpoint. A non-204 answer means a portal is in
  // the way. Detection only - never submits credentials to a portal.
  CaptivePortalResult checkCaptivePortal(Stream &out);

  // Browse the joined LAN for advertised services (mDNS). Returns count.
  uint8_t discoverServices(ServiceRecord out_[], uint8_t maxRecords, Stream &out);

  // TCP connect sweep of `target` (host or IP) over a small common-port set.
  // Empty target falls back to the current gateway. Returns count probed.
  uint8_t portScan(const String &target, PortResult out_[], uint8_t maxRecords,
                   Stream &out);

  // --- Recon (observational; needs a link on real builds) ---
  // Resolve a hostname to an IP. Returns true and fills ipOut on success.
  bool dnsLookup(const String &host, String &ipOut, Stream &out);
  // HTTP GET a URL and extract the <title> and Server header (banner grab).
  bool httpBanner(const String &url, String &titleOut, String &serverOut, Stream &out);
  // TCP connect sweep of the gateway /24 across last-octet [start..end] on
  // `port`, collecting live hosts. Bounded so it stays quick. Returns live count.
  uint8_t hostSweep(uint8_t start, uint8_t end, uint16_t port, String liveOut[],
                    uint8_t maxRecords, Stream &out);
  // Best-effort vendor for a MAC/BSSID from a small built-in OUI table.
  static String ouiVendor(const String &mac);

 private:
  WifiLinkStatus link_;
};

#endif
