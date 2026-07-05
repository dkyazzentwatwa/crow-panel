#ifndef CROW_PANEL_NETWORK_CLIENT_H
#define CROW_PANEL_NETWORK_CLIENT_H

#include <Arduino.h>
#include "AppConfig.h"

#if USE_WIFI
#include <WiFi.h>
#endif

class NetworkClient {
 public:
  void begin(const char *endpoint);
  bool postEvent(const String &eventJson);
  String postSummaryRequest(const String &prompt);

 private:
  String endpoint_ = "http://localhost:8787";
};

#endif
