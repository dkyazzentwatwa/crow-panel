#ifndef CROW_PANEL_WORLD_FEED_CLIENT_H
#define CROW_PANEL_WORLD_FEED_CLIENT_H

#include <Arduino.h>
#include "AppConfig.h"
#include "Logger.h"
#include "Throttle.h"
#include "WorldFeed.h"

// Pulls weather / earthquake / aurora / air-quality data from public HTTPS
// JSON APIs and exposes it as a WorldFeeds snapshot. Direct on-device fetch
// uses WiFiClientSecure + setInsecure. poll() issues at most one request per
// call and rotates feeds so they never fire together. With USE_WIFI=0 it fills
// canned values so dashboards render offline.
class WorldFeedClient {
 public:
  void begin(float lat, float lon, const char *place, int kpThreshold);
  bool poll(WorldFeeds &out);                          // true if a feed changed this call
  bool refresh(WorldFeeds &out, const String &which);  // "all"|"weather"|"quake"|"aurora"|"air"

 private:
  void fillMock();
  AuroraVerdict verdictFor(float kp) const;
#if USE_WIFI
  bool fetchWeather();
  bool fetchQuake();
  bool fetchAurora();
  bool fetchAirQuality();
  bool setError(String &slot, const String &message);
  void clearError(String &slot);
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
  Throttle airGate_{20UL * 60UL * 1000UL};
  // Until a feed's first success, attempts are spaced by this instead.
  Throttle startupGate_{5000};
  uint8_t turn_ = 0;  // rotates which feed is eligible, so at most one/call
};

#endif
