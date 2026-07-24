#include "WifiScanner.h"
#include <CrowPanelShared.h>

#if USE_WIFI_SCAN
#include <WiFi.h>
#endif

namespace {
uint8_t boundedCount(uint8_t requested, uint8_t available) {
  return requested < available ? requested : available;
}

#if USE_WIFI_SCAN
const char *authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return "open";
    case WIFI_AUTH_WEP:
      return "wep";
    case WIFI_AUTH_WPA_PSK:
      return "wpa";
    case WIFI_AUTH_WPA2_PSK:
      return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "wpa/wpa2";
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "wpa2-enterprise";
#endif
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK:
      return "wpa3";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "wpa2/wpa3";
#endif
    default:
      return "unknown";
  }
}
#endif
}  // namespace

void WifiScanner::begin() {
#if USE_WIFI_SCAN
  configureCrowPanelHostedWiFiPins("wifi-scan");
  Logger::info("wifi-scan", "USE_WIFI_SCAN=1; Arduino WiFi passive scan path enabled");
#else
  Logger::info("wifi-scan", "mock Wi-Fi scan path enabled");
#endif
}

uint8_t WifiScanner::scan(WifiNetworkRecord records[], uint8_t maxRecords, Stream &out) {
  if (maxRecords == 0) return 0;

#if USE_WIFI_SCAN
  configureCrowPanelHostedWiFiPins("wifi-scan");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  int found = WiFi.scanNetworks(false, false, true, CYPHERDRIVE_WIFI_SCAN_MS_PER_CHANNEL);
  if (found < 0) {
    out.print(F("[scan:wifi] hosted passive scan failed code="));
    out.println(found);
    return 0;
  }

  uint8_t available = found > 255 ? 255 : (uint8_t)found;
  uint8_t count = boundedCount(maxRecords, available);
  for (uint8_t i = 0; i < count; ++i) {
    records[i].ssid = WiFi.SSID(i);
    records[i].hidden = records[i].ssid.length() == 0;
    if (records[i].hidden) records[i].ssid = "(hidden)";
    records[i].rssi = WiFi.RSSI(i);
    records[i].channel = (uint8_t)WiFi.channel(i);
    records[i].auth = authName(WiFi.encryptionType(i));

    out.print(F("[scan:wifi] "));
    out.print(records[i].ssid);
    out.print(F(" ch="));
    out.print(records[i].channel);
    out.print(F(" rssi="));
    out.print(records[i].rssi);
    out.print(F(" auth="));
    out.print(records[i].auth);
    out.println(F(" source=hosted-c6-passive"));
  }
  WiFi.scanDelete();
  return count;
#else
  // Believable mock spread: 2.4 GHz + 5 GHz, mixed security, one hidden SSID,
  // so every panel screen fills with no radio activity at all.
  struct MockNet {
    const char *ssid;
    uint8_t channel;
    int32_t rssi;
    const char *auth;
    bool hidden;
  };
  static const MockNet kMock[] = {
      {"StudioNet", 6, -41, "wpa2", false},
      {"StudioNet-5G", 44, -55, "wpa3", false},
      {"GuestLab", 11, -63, "open", false},
      {"PanelBench", 1, -72, "wpa2", false},
      {"CafeMesh", 36, -68, "wpa2/wpa3", false},
      {"(hidden)", 149, -77, "wpa2", true},
      {"IoT-Legacy", 3, -81, "wep", false},
      {"NeighborNet", 9, -88, "wpa", false},
  };
  const uint8_t available = sizeof(kMock) / sizeof(kMock[0]);
  uint8_t count = boundedCount(maxRecords, available);
  for (uint8_t i = 0; i < count; ++i) {
    records[i].ssid = kMock[i].ssid;
    records[i].channel = kMock[i].channel;
    records[i].rssi = kMock[i].rssi;
    records[i].auth = kMock[i].auth;
    records[i].hidden = kMock[i].hidden;
  }

  for (uint8_t i = 0; i < count; ++i) {
    out.print(F("[scan:wifi] "));
    out.print(records[i].ssid);
    out.print(F(" ch="));
    out.print(records[i].channel);
    out.print(F(" rssi="));
    out.print(records[i].rssi);
    out.print(F(" auth="));
    out.print(records[i].auth);
    out.println(F(" source=mock"));
  }
  return count;
#endif
}

const char *WifiScanner::driverName() const {
#if USE_WIFI_SCAN
  return "hosted-c6-passive";
#else
  return "mock";
#endif
}

bool WifiScanner::hardwareEnabled() const {
  return USE_WIFI_SCAN == 1;
}
