#include "HostedWiFi.h"

#include "AppConfig.h"
#include "HardwareProfile.h"
#include "Logger.h"

#if (USE_WIFI || USE_WIFI_SCAN || USE_WIFI_ACTIVE || USE_FLOCK_C6_WITNESS || USE_RF_LAB_C6_WIFI) && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <WiFi.h>
#endif

bool configureCrowPanelHostedWiFiPins(const char *scope) {
#if (USE_WIFI || USE_WIFI_SCAN || USE_WIFI_ACTIVE || USE_FLOCK_C6_WITNESS || USE_RF_LAB_C6_WIFI) && defined(CONFIG_IDF_TARGET_ESP32P4)
  static bool attempted = false;
  static bool configured = false;

  const char *logScope = (scope != nullptr) ? scope : "wifi";
  if (attempted) {
    return configured;
  }

  const HostedSdioPins &hp = activeHardwareProfile().hostedSdio;
  configured = WiFi.setPins(hp.clk, hp.cmd, hp.d0, hp.d1, hp.d2, hp.d3, hp.reset);
  attempted = true;

  if (configured) {
    Logger::info(logScope, "configured CrowPanel C6 SDIO pins");
  } else {
    Logger::warn(logScope, "WiFi.setPins failed (WiFi already initialized?)");
  }
  return configured;
#else
  (void)scope;
  return true;
#endif
}
