#include "WorldFeedClient.h"

// Fetch path is COMPILE-VERIFIED, NOT HARDWARE-VERIFIED. HTTPS rides the
// onboard ESP32-C6 over esp_hosted. With USE_WIFI=0 the whole thing is a
// canned data source so the dashboard strip renders offline.

void WorldFeedClient::begin(float lat, float lon, const char *place, int kpThreshold) {
  lat_ = lat;
  lon_ = lon;
  place_ = (place != nullptr) ? place : "";
  kpThreshold_ = kpThreshold;
#if USE_WIFI
  // UTC epoch so quake "age" can be computed once the clock syncs.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Logger::info("world", "feeds: direct HTTPS, loc=" + place_);
#else
  Logger::info("world", "feeds: mock (USE_WIFI=0), loc=" + place_);
#endif
}

AuroraVerdict WorldFeedClient::verdictFor(float kp) const {
  if (kp >= (float)kpThreshold_) return kAuroraLikely;
  if (kp >= (float)kpThreshold_ - 1.0f) return kAuroraWatch;
  return kAuroraQuiet;
}

void WorldFeedClient::fillMock() {
  feeds_.weather = WeatherData();
  feeds_.weather.tempC = 14.0f;
  feeds_.weather.feelsC = 12.5f;
  feeds_.weather.windKt = 6.0f;
  feeds_.weather.hiC = 18.0f;
  feeds_.weather.loC = 7.0f;
  feeds_.weather.condition = "Partly cloudy";
  feeds_.weatherValid = true;
  feeds_.weatherMs = millis();

  feeds_.quake = QuakeData();
  feeds_.quake.mag = 4.6f;
  feeds_.quake.depthKm = 33.0f;
  feeds_.quake.place = "near Coquimbo, Chile";
  feeds_.quake.ageMin = 12;
  feeds_.quake.count24h = 18;
  feeds_.quakeValid = true;
  feeds_.quakeMs = millis();

  feeds_.aurora = AuroraData();
  feeds_.aurora.kp = 5.0f;
  feeds_.aurora.level = "G1";
  feeds_.aurora.verdict = verdictFor(5.0f);
  feeds_.aurora.trend = 1;
  feeds_.auroraValid = true;
  feeds_.auroraMs = millis();
}

bool WorldFeedClient::poll(WorldFeeds &out) {
#if USE_WIFI
  uint8_t which = turn_ % 3;
  turn_++;
  bool changed = false;
  switch (which) {
    case 0:
      if (feeds_.weatherValid ? weatherGate_.ready() : startupGate_.ready()) changed = fetchWeather();
      break;
    case 1:
      if (feeds_.quakeValid ? quakeGate_.ready() : startupGate_.ready()) changed = fetchQuake();
      break;
    case 2:
      if (feeds_.auroraValid ? auroraGate_.ready() : startupGate_.ready()) changed = fetchAurora();
      break;
  }
  if (changed) out = feeds_;
  return changed;
#else
  if (!primed_) {
    fillMock();
    primed_ = true;
    out = feeds_;
    return true;
  }
  return false;
#endif
}

bool WorldFeedClient::refresh(WorldFeeds &out, const String &which) {
#if USE_WIFI
  bool any = false;
  if (which == "all" || which == "weather") any = fetchWeather() || any;
  if (which == "all" || which == "quake")   any = fetchQuake() || any;
  if (which == "all" || which == "aurora")  any = fetchAurora() || any;
  out = feeds_;
  return any;
#else
  (void)which;
  fillMock();
  out = feeds_;
  return true;
#endif
}

#if USE_WIFI
// Real implementations land in Task 6. Stubs keep USE_WIFI builds linking.
bool WorldFeedClient::fetchWeather() { return false; }
bool WorldFeedClient::fetchQuake() { return false; }
bool WorldFeedClient::fetchAurora() { return false; }
#endif
