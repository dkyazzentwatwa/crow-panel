#ifndef CYPHERDRIVE_WIRELESS_TYPES_H
#define CYPHERDRIVE_WIRELESS_TYPES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

struct WifiNetworkRecord {
  String ssid;
  int32_t rssi = 0;
  uint8_t channel = 0;
  String auth;
  bool hidden = false;
};

struct BleAdvertisementRecord {
  String label;
  String address;
  int32_t rssi = 0;
  String vendor;
  String detail;
};

#endif
