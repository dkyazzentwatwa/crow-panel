#include "WiFiSurveyScanner.h"

#if USE_WIFI_SCAN
#if defined(__has_include)
#if __has_include(<WiFi.h>)
#include <WiFi.h>
#define SURVEYOPS_HAS_WIFI_SCAN_DRIVER 1
#else
#define SURVEYOPS_HAS_WIFI_SCAN_DRIVER 0
#endif
#else
#define SURVEYOPS_HAS_WIFI_SCAN_DRIVER 0
#endif
#else
#define SURVEYOPS_HAS_WIFI_SCAN_DRIVER 0
#endif

#if USE_WIFI_SCAN && SURVEYOPS_HAS_WIFI_SCAN_DRIVER
namespace {
String authName(wifi_auth_mode_t auth) {
  if (auth == WIFI_AUTH_OPEN) {
    return "OPEN";
  }
#ifdef WIFI_AUTH_WEP
  if (auth == WIFI_AUTH_WEP) {
    return "WEP";
  }
#endif
  if (auth == WIFI_AUTH_WPA_PSK) {
    return "WPA";
  }
  if (auth == WIFI_AUTH_WPA2_PSK) {
    return "WPA2";
  }
  if (auth == WIFI_AUTH_WPA_WPA2_PSK) {
    return "WPA/WPA2";
  }
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
  if (auth == WIFI_AUTH_WPA2_ENTERPRISE) {
    return "WPA2-ENT";
  }
#endif
#ifdef WIFI_AUTH_WPA3_PSK
  if (auth == WIFI_AUTH_WPA3_PSK) {
    return "WPA3";
  }
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
  if (auth == WIFI_AUTH_WPA2_WPA3_PSK) {
    return "WPA2/WPA3";
  }
#endif
  return "UNKNOWN";
}
}  // namespace
#endif

void WiFiSurveyScanner::begin() {
#if USE_WIFI_SCAN
#if SURVEYOPS_HAS_WIFI_SCAN_DRIVER
  configureCrowPanelHostedWiFiPins("wifi-scan");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  ready_ = true;
  detail_ = "station scan mode; no joins";
  Logger::info("wifi-scan", "passive Wi-Fi scan enabled; no join or credential capture");
#else
  ready_ = false;
  detail_ = "WiFi.h not available";
  Logger::error("wifi-scan", "USE_WIFI_SCAN=1 but WiFi.h was not found");
#endif
#else
  ready_ = true;
  detail_ = "mock AP rows";
  Logger::info("wifi-scan", "mock Wi-Fi scan active (USE_WIFI_SCAN=0)");
#endif
}

uint8_t WiFiSurveyScanner::scan(WifiApRecord *rows, uint8_t maxRows) {
  if (rows == nullptr || maxRows == 0) {
    return 0;
  }
  scanCount_++;

#if USE_WIFI_SCAN
#if SURVEYOPS_HAS_WIFI_SCAN_DRIVER
  if (!ready_) {
    begin();
  }
  int16_t found = WiFi.scanNetworks(false, true, true, 300);
  if (found <= 0) {
    detail_ = found == 0 ? "no AP rows returned" : String("scan failed code=") + String(found);
    WiFi.scanDelete();
    return 0;
  }

  uint8_t count = found < maxRows ? (uint8_t)found : maxRows;
  for (uint8_t i = 0; i < count; i++) {
    rows[i].ssid = WiFi.SSID(i);
    rows[i].hidden = rows[i].ssid.length() == 0;
    if (rows[i].hidden) {
      rows[i].ssid = "(hidden)";
    }
    rows[i].bssid = WiFi.BSSIDstr(i);
    rows[i].rssi = WiFi.RSSI(i);
    rows[i].channel = WiFi.channel(i);
    rows[i].authMode = authName(WiFi.encryptionType(i));
    rows[i].seenAtMs = millis();
  }
  WiFi.scanDelete();
  detail_ = String(count) + " passive rows";
  return count;
#else
  detail_ = "WiFi.h not available";
  return 0;
#endif
#else
  return loadMockRows(rows, maxRows);
#endif
}

const char *WiFiSurveyScanner::driverName() const {
#if USE_WIFI_SCAN
#if SURVEYOPS_HAS_WIFI_SCAN_DRIVER
  return "wifi-passive-scan";
#else
  return "wifi-scan-missing";
#endif
#else
  return "mock";
#endif
}

String WiFiSurveyScanner::statusLine() const {
  return String("[wifi] driver=") + driverName() +
         " ready=" + (ready_ ? "yes" : "no") +
         " scans=" + String(scanCount_) +
         " detail=" + detail_;
}

uint8_t WiFiSurveyScanner::loadMockRows(WifiApRecord *rows, uint8_t maxRows) {
  const uint8_t count = maxRows < 3 ? maxRows : 3;
  if (count > 0) {
    rows[0].ssid = "StudioNet";
    rows[0].bssid = "02:11:22:33:44:55";
    rows[0].authMode = "WPA2";
    rows[0].rssi = -42;
    rows[0].channel = 6;
    rows[0].seenAtMs = millis();
  }
  if (count > 1) {
    rows[1].ssid = "GuestLab";
    rows[1].bssid = "02:AA:BB:CC:DD:01";
    rows[1].authMode = "OPEN";
    rows[1].rssi = -67;
    rows[1].channel = 11;
    rows[1].seenAtMs = millis();
  }
  if (count > 2) {
    rows[2].ssid = "MakerAP";
    rows[2].bssid = "02:AA:BB:CC:DD:02";
    rows[2].authMode = "WPA3";
    rows[2].rssi = -73;
    rows[2].channel = 1;
    rows[2].seenAtMs = millis();
  }
  detail_ = String(count) + " mock rows";
  return count;
}
