#ifndef RELAYOPS_WORLD_FEED_CLIENT_H
#define RELAYOPS_WORLD_FEED_CLIENT_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>   // Throttle, Logger
#include "WorldFeed.h"

// Pulls weather / earthquake / aurora data from public HTTPS JSON APIs and
// exposes it as a WorldFeeds snapshot for the dashboard. Direct on-device
// fetch (WiFiClientSecure + setInsecure). Non-blocking: poll() issues at
// most ONE HTTPS request per call, and only when that feed is due; the
// three are rotated so they never fire together. With USE_WIFI=0 it fills
// canned values so the strip renders offline.
class WorldFeedClient {
 public:
  void begin(float lat, float lon, const char *place, int kpThreshold);
  bool poll(WorldFeeds &out);                          // true if a feed changed this call
  bool refresh(WorldFeeds &out, const String &which);  // "all"|"weather"|"quake"|"aurora"

 private:
  void fillMock();
  AuroraVerdict verdictFor(float kp) const;
#if USE_WIFI
  bool fetchWeather();
  bool fetchQuake();
  bool fetchAurora();
#endif

  WorldFeeds feeds_;
  float lat_ = 0.0f, lon_ = 0.0f;
  String place_ = "";
  int kpThreshold_ = 6;
  bool primed_ = false;

  // Normal cadence per feed once primed.
  Throttle weatherGate_{15UL * 60UL * 1000UL};
  Throttle quakeGate_{5UL * 60UL * 1000UL};
  Throttle auroraGate_{10UL * 60UL * 1000UL};
  // Until a feed's first success, attempts are spaced by this instead (so a
  // feed appears seconds after boot, not one full interval later).
  Throttle startupGate_{5000};
  uint8_t turn_ = 0;  // rotates which feed is eligible, so at most one/call
};

#endif
