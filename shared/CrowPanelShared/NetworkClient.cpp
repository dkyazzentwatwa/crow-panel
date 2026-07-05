#include "NetworkClient.h"
#include "Logger.h"

void NetworkClient::begin(const char *endpoint) {
  endpoint_ = endpoint;
#if USE_WIFI
  Logger::warn("network", "USE_WIFI is enabled, but Wi-Fi connection setup is still project-specific.");
#else
  Logger::info("network", "mock endpoint=" + endpoint_);
#endif
}

bool NetworkClient::postEvent(const String &eventJson) {
  Logger::info("network", "POST /events " + eventJson);
  return true;
}

String NetworkClient::postSummaryRequest(const String &prompt) {
  Logger::info("network", "POST /summary prompt=" + prompt);
  return "Mock summary: conditions are stable, one item needs review.";
}
