#include "C6RadioMonitor.h"
#include "../config/ProjectConfig.h"
#include <CrowPanelShared.h>

#if USE_RF_LAB_C6_WIFI && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <WiFi.h>
#define RF_LAB_HAS_C6_WIFI 1
#else
#define RF_LAB_HAS_C6_WIFI 0
#endif

void C6RadioMonitor::begin() {
  state_.wifiEnabled = USE_RF_LAB_C6_WIFI == 1;
  state_.bleEnabled = USE_RF_LAB_C6_BLE == 1;
#if RF_LAB_HAS_C6_WIFI
  if (configureCrowPanelHostedWiFiPins("rflab-c6-wifi")) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    state_.wifiReady = true;
    state_.wifiStatus = "ready; aggregate scan queued";
    wifiRequested_ = true;
    nextWifiScanMs_ = millis() + 250;
  } else {
    state_.wifiStatus = "hosted SDIO pin setup failed";
  }
#else
  state_.wifiStatus = state_.wifiEnabled ? "P4/C6 Wi-Fi build unavailable" : "C6 Wi-Fi disabled";
#endif

#if USE_RF_LAB_C6_BLE
  // The installed P4 Arduino profile does not enable esp_hosted NimBLE.
  // Keep this a separate, explicit page until C6 firmware exposes that path.
  state_.bleStatus = "C6 hosted NimBLE firmware required";
#else
  state_.bleStatus = "C6 BLE disabled";
#endif
}

void C6RadioMonitor::tick() {
#if RF_LAB_HAS_C6_WIFI
  if (!state_.wifiReady) return;
  if (state_.wifiScanning) {
    int16_t result = WiFi.scanComplete();
    if (result >= 0) {
      consumeWifiScan_(result);
      WiFi.scanDelete();
      state_.wifiScanning = false;
      ++state_.wifiScans;
      nextWifiScanMs_ = millis() + RF_LAB_C6_SCAN_INTERVAL_MS;
      state_.wifiStatus = "aggregate passive scan complete";
    } else if (result == WIFI_SCAN_FAILED || millis() - wifiStartedMs_ > 12000UL) {
      WiFi.scanDelete();
      state_.wifiScanning = false;
      nextWifiScanMs_ = millis() + 5000UL;
      state_.wifiStatus = "passive scan failed";
    }
    return;
  }
  if (wifiRequested_ || millis() >= nextWifiScanMs_) startWifiScan_();
#endif
}

void C6RadioMonitor::requestWifiScan() {
#if RF_LAB_HAS_C6_WIFI
  wifiRequested_ = true;
#endif
}

void C6RadioMonitor::startWifiScan_() {
#if RF_LAB_HAS_C6_WIFI
  if (state_.wifiScanning) return;
  wifiRequested_ = false;
  int16_t result = WiFi.scanNetworks(true, true, true, 250);
  if (result == WIFI_SCAN_RUNNING) {
    state_.wifiScanning = true;
    wifiStartedMs_ = millis();
    state_.wifiStatus = "aggregate passive scan running";
  } else if (result >= 0) {
    consumeWifiScan_(result);
    WiFi.scanDelete();
    ++state_.wifiScans;
    nextWifiScanMs_ = millis() + RF_LAB_C6_SCAN_INTERVAL_MS;
    state_.wifiStatus = "aggregate passive scan complete";
  } else {
    nextWifiScanMs_ = millis() + 5000UL;
    state_.wifiStatus = "passive scan start failed";
  }
#endif
}

void C6RadioMonitor::consumeWifiScan_(int16_t found) {
#if RF_LAB_HAS_C6_WIFI
  state_.wifiNetworks = found > 0 ? (uint16_t)found : 0;
  state_.wifiStrongestRssi = -127;
  for (int16_t index = 0; index < found; ++index) {
    state_.wifiStrongestRssi = max(state_.wifiStrongestRssi, (int8_t)WiFi.RSSI(index));
  }
#else
  (void)found;
#endif
}
