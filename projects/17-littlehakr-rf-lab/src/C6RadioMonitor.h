#ifndef LITTLEHAKR_RF_LAB_C6_RADIO_MONITOR_H
#define LITTLEHAKR_RF_LAB_C6_RADIO_MONITOR_H

#include <Arduino.h>

struct C6RadioSnapshot {
  bool wifiEnabled = false;
  bool wifiReady = false;
  bool wifiScanning = false;
  uint16_t wifiNetworks = 0;
  int8_t wifiStrongestRssi = -127;
  uint32_t wifiScans = 0;
  bool bleEnabled = false;
  bool bleAvailable = false;
  uint32_t bleReports = 0;
  const char *wifiStatus = "disabled";
  const char *bleStatus = "disabled";
};

class C6RadioMonitor {
 public:
  void begin();
  void tick();
  void requestWifiScan();
  const C6RadioSnapshot &snapshot() const { return state_; }

 private:
  void startWifiScan_();
  void consumeWifiScan_(int16_t found);

  C6RadioSnapshot state_;
  uint32_t nextWifiScanMs_ = 0;
  uint32_t wifiStartedMs_ = 0;
  bool wifiRequested_ = false;
};

#endif
