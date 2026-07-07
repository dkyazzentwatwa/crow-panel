// Wi-Fi path: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8).
// NOT HARDWARE-VERIFIED. The ESP32-P4 has no radio of its own - Wi-Fi
// rides the onboard ESP32-C6 over SDIO (esp_hosted, bundled in the core).
// If the link never comes up on real hardware, the C6's hosted firmware
// version may not match the core's client; Elecrow ships upgrade guides -
// see docs/hardware-bringup-checklist.md Stage 5.

#include "CrowNetworkClient.h"
#include "Logger.h"

#if USE_WIFI
#include <HTTPClient.h>
#endif

void CrowNetworkClient::begin(const char *endpoint, const char *ssid, const char *password) {
  endpoint_ = endpoint;
  ssid_ = (ssid != nullptr) ? ssid : "";
  password_ = (password != nullptr) ? password : "";
#if USE_WIFI
  if (ssid_.length() == 0) {
    Logger::warn("network", "USE_WIFI=1 but no credentials; copy config/WiFiSecrets.example.h to WiFiSecrets.h. Running log-only.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_.c_str(), password_.c_str());
  Logger::info("network", "wifi connecting to \"" + ssid_ + "\", endpoint=" + endpoint_);
#else
  Logger::info("network", "mock endpoint=" + endpoint_);
#endif
}

void CrowNetworkClient::maintain() {
#if USE_WIFI
  if (ssid_.length() == 0) {
    return;
  }
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !wasConnected_) {
    Logger::info("network", "wifi connected, ip=" + WiFi.localIP().toString());
  } else if (!up && wasConnected_) {
    Logger::warn("network", "wifi connection lost, retrying");
  } else if (!up && retry_.ready()) {
    WiFi.reconnect();
    Logger::info("network", "wifi retry...");
  }
  wasConnected_ = up;
#endif
}

bool CrowNetworkClient::connected() const {
#if USE_WIFI
  return WiFi.status() == WL_CONNECTED;
#else
  return false;
#endif
}

bool CrowNetworkClient::postEvent(const String &eventJson) {
#if USE_WIFI
  if (connected()) {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.begin(endpoint_ + "/events");
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(eventJson);
    http.end();
    if (code > 0) {
      Logger::info("network", "POST /events -> " + String(code));
      return code >= 200 && code < 300;
    }
    Logger::error("network", "POST /events failed: " + HTTPClient::errorToString(code));
    return false;
  }
  // Disconnected: fall through to log-only so demos keep running offline.
#endif
  Logger::info("network", "POST /events " + eventJson);
  return true;
}

String CrowNetworkClient::postSummaryRequest(const String &prompt) {
#if USE_WIFI
  if (connected()) {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.begin(endpoint_ + "/summary");
    http.addHeader("Content-Type", "application/json");
    // Naive JSON, unescaped quotes - fine against mock-api; use a real
    // JSON library before pointing this at anything else.
    int code = http.POST(String("{\"prompt\":\"") + prompt + "\"}");
    String body = (code > 0) ? http.getString() : "";
    http.end();
    if (code >= 200 && code < 300) {
      // Raw response body; the mock API returns {"summary": ..., ...}.
      return body;
    }
    Logger::warn("network", "POST /summary -> " + String(code) + ", using local fallback");
  }
#endif
  Logger::info("network", "POST /summary prompt=" + prompt);
  return "Mock summary: conditions are stable, one item needs review.";
}
