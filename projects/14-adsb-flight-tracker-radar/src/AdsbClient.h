#ifndef ADSB_CLIENT_H
#define ADSB_CLIENT_H

#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowNetworkClient, Throttle, Logger
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "AdsbTypes.h"
#include "AircraftStore.h"

// Live ADS-B fetcher: GETs the airplanes.live / adsb.fi point API, parses it
// with a filtered, PSRAM-backed ArduinoJson document, derives distance/bearing
// from home, and upserts into the shared AircraftStore. All HTTP/JSON is gated
// on USE_WIFI; in mock builds every method is a cheap no-op (the sketch uses
// MockAdsbSource instead), so this class still compiles in the baseline build.
class AdsbClient {
 public:
  void begin(CrowNetworkClient *net, AircraftStore *store,
             double homeLat, double homeLon, float rangeKm, int radiusNm,
             const char *apiBase, const char *userAgent,
             const char *ssid, const char *pass);

  void setPollIntervalMs(uint32_t ms);  // clamped to >= 1000 ms

  // One fetch + parse + upsert. Returns true when the store was refreshed.
  // Safe from a task or from loop(); enforces a hard ~1 req/s floor.
  bool pollOnce();

  // Spawn the background poll task pinned to core 0 (USE_WIFI only).
  void startBackgroundTask();

  // Task stack headroom (words) for `status`; 0 when no task is running.
  uint32_t taskStackHighWater() const;

#if USE_WIFI
  void taskLoop();  // internal; public only so the task trampoline can reach it
#endif

 private:
  CrowNetworkClient *net_ = nullptr;
  AircraftStore *store_ = nullptr;
  double homeLat_ = 0;
  double homeLon_ = 0;
  float rangeKm_ = 100.0f;
  int radiusNm_ = 54;
  const char *apiBase_ = "";
  const char *userAgent_ = nullptr;
  const char *ssid_ = "";
  const char *pass_ = "";
  const char *sourceLabel_ = "adsb";
  volatile uint32_t pollMs_ = 5000;
  TaskHandle_t taskHandle_ = nullptr;
  Throttle minGap_{1000};
};

#endif
