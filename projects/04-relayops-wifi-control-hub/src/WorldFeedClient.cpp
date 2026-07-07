#include "WorldFeedClient.h"

// Fetch path is COMPILE-VERIFIED, NOT HARDWARE-VERIFIED. HTTPS rides the
// onboard ESP32-C6 over esp_hosted. With USE_WIFI=0 the whole thing is a
// canned data source so the dashboard strip renders offline.

#if USE_WIFI
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

namespace {
// WMO weather codes (Open-Meteo) -> short text.
const char *weatherText(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 99: return "Thunderstorm";
    default: return "--";
  }
}
// NOAA G-scale label for a planetary Kp.
const char *gLevel(float kp) {
  int k = (int)(kp + 0.5f);
  if (k >= 9) return "G5";
  if (k == 8) return "G4";
  if (k == 7) return "G3";
  if (k == 6) return "G2";
  if (k == 5) return "G1";
  return "calm";
}
}  // namespace
#endif

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
  // One feed per call, rotated, so at most one HTTPS request per loop pass.
  // Known trade-off: a primed feed whose gate fires but whose fetch then fails
  // has already consumed its interval (Throttle::ready() advances on true), so
  // it waits the full interval to retry. Acceptable for v1 - last-good data
  // stays on screen and dims via the staleness rule.
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
bool WorldFeedClient::fetchWeather() {
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(lat_, 4) +
               "&longitude=" + String(lon_, 4) +
               "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m" +
               "&daily=temperature_2m_max,temperature_2m_min&wind_speed_unit=kn" +
               "&temperature_unit=celsius&timezone=auto&forecast_days=1";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "weather HTTP " + String(code));
    return false;
  }

  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["apparent_temperature"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["wind_speed_10m"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Logger::warn("world", String("weather json ") + err.c_str());
    return false;
  }

  feeds_.weather.tempC = doc["current"]["temperature_2m"] | NAN;
  feeds_.weather.feelsC = doc["current"]["apparent_temperature"] | NAN;
  feeds_.weather.windKt = doc["current"]["wind_speed_10m"] | NAN;
  feeds_.weather.condition = weatherText(doc["current"]["weather_code"] | -1);
  feeds_.weather.hiC = doc["daily"]["temperature_2m_max"][0] | NAN;
  feeds_.weather.loC = doc["daily"]["temperature_2m_min"][0] | NAN;
  feeds_.weatherValid = true;
  feeds_.weatherMs = millis();
  Logger::info("world", "weather " + String(feeds_.weather.tempC, 1) + "C " + feeds_.weather.condition);
  return true;
}

bool WorldFeedClient::fetchQuake() {
  const char *url = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "quake HTTP " + String(code));
    return false;
  }

  // Filter keeps only newest-event fields + the 24h count. In a GeoJSON
  // "past day" feed features are ordered newest-first, so features[0] is the
  // most recent. The filter's [0] pattern applies to every element, which is
  // fine - the feed is small.
  JsonDocument filter;
  filter["metadata"]["count"] = true;
  filter["features"][0]["properties"]["mag"] = true;
  filter["features"][0]["properties"]["place"] = true;
  filter["features"][0]["properties"]["time"] = true;
  filter["features"][0]["geometry"]["coordinates"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Logger::warn("world", String("quake json ") + err.c_str());
    return false;
  }

  feeds_.quake.count24h = doc["metadata"]["count"] | -1;
  JsonArray feats = doc["features"].as<JsonArray>();
  if (feats.isNull() || feats.size() == 0) {
    Logger::warn("world", "quake feed empty");
    return false;
  }
  JsonObject f0 = feats[0];
  feeds_.quake.mag = f0["properties"]["mag"] | NAN;
  feeds_.quake.place = (const char *)(f0["properties"]["place"] | "");
  feeds_.quake.depthKm = f0["geometry"]["coordinates"][2] | NAN;

  long long tms = f0["properties"]["time"] | 0LL;  // epoch milliseconds
  time_t now = time(nullptr);
  if (now > 1600000000L && tms > 0) {
    feeds_.quake.ageMin = (long)((now - (time_t)(tms / 1000)) / 60);
  } else {
    feeds_.quake.ageMin = -1;
  }
  feeds_.quakeValid = true;
  feeds_.quakeMs = millis();
  Logger::info("world", "quake M" + String(feeds_.quake.mag, 1) + " " + feeds_.quake.place);
  return true;
}

bool WorldFeedClient::fetchAurora() {
  const char *url = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "aurora HTTP " + String(code));
    return false;
  }

  // products/noaa-planetary-k-index.json is an array of OBJECTS (no header
  // row) with a numeric "Kp" field (capital K, endpoint-specific - sibling
  // feeds use lowercase); the last element is the most recent 3-hour reading.
  // Parsed without a Filter on purpose: we need positional tail access (last
  // two elements), which a key-based filter can't express, and the series is
  // small enough for 32 MB PSRAM.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Logger::warn("world", String("aurora json ") + err.c_str());
    return false;
  }
  JsonArray rows = doc.as<JsonArray>();
  if (rows.isNull() || rows.size() < 2) {
    Logger::warn("world", "aurora feed short");
    return false;
  }
  JsonObject last = rows[rows.size() - 1];
  JsonObject prev = rows[rows.size() - 2];
  float kp = last["Kp"] | 0.0f;
  float kpPrev = prev["Kp"] | 0.0f;

  feeds_.aurora.kp = kp;
  feeds_.aurora.level = gLevel(kp);
  feeds_.aurora.verdict = verdictFor(kp);
  feeds_.aurora.trend = (kp > kpPrev) ? 1 : (kp < kpPrev) ? -1 : 0;
  feeds_.auroraValid = true;
  feeds_.auroraMs = millis();
  Logger::info("world", "aurora Kp " + String(kp, 1) + " " + feeds_.aurora.level);
  return true;
}
#endif
