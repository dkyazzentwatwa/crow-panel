#ifndef CROW_PANEL_NETWORK_CLIENT_H
#define CROW_PANEL_NETWORK_CLIENT_H

#include <Arduino.h>
#include "AppConfig.h"
#include "Throttle.h"

#if USE_WIFI
#include <WiFi.h>
#endif

// Named CrowNetworkClient (not NetworkClient) because the ESP32 Arduino
// core's Network library already defines a class NetworkClient, which
// WiFi.h pulls into every USE_WIFI build.
//
// Members are declared unconditionally so the class layout is identical in
// every translation unit no matter how USE_WIFI is set - only the method
// bodies (one TU) change with the flag.
class CrowNetworkClient {
 public:
  // ssid/password are ignored unless built with USE_WIFI=1
  // (EXTRA_FLAGS="-DUSE_WIFI=1"); they normally come from the project's
  // config/WiFiSecrets.h via ProjectConfig.h.
  void begin(const char *endpoint, const char *ssid = nullptr, const char *password = nullptr);

  // Call once per loop(). Non-blocking Wi-Fi connection state machine;
  // no-op in mock builds.
  void maintain();

  bool connected() const;

  bool postEvent(const String &eventJson);
  String postSummaryRequest(const String &prompt);

 private:
  String endpoint_ = "http://localhost:8787";
  String ssid_ = "";
  String password_ = "";
  Throttle retry_{5000};
  bool wasConnected_ = false;
};

#endif
