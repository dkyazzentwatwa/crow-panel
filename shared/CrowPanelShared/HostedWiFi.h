#ifndef CROW_PANEL_HOSTED_WIFI_H
#define CROW_PANEL_HOSTED_WIFI_H

#include <Arduino.h>

// Applies the CrowPanel ESP32-P4 -> ESP32-C6 SDIO pin remap before Arduino
// WiFi starts esp_hosted. Safe to call more than once; required before any
// direct WiFi.* use on hosted-C6 paths.
bool configureCrowPanelHostedWiFiPins(const char *scope = "wifi");

#endif
