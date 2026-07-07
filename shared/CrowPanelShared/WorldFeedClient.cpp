#include "WorldFeedClient.h"

// Fetch path uses the ESP32-P4's hosted C6 Wi-Fi link. With USE_WIFI=0 the
// whole class is a canned data source so dashboards render offline.

#if USE_WIFI
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace {
const char *weatherText(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1:
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Freezing rain";
    case 71:
    case 73:
    case 75:
    case 77: return "Snow";
    case 80:
    case 81:
    case 82: return "Showers";
    case 85:
    case 86: return "Snow showers";
    case 95:
    case 96:
    case 99: return "Thunderstorm";
    default: return "--";
  }
}

const char *gLevel(float kp) {
  int k = (int)(kp + 0.5f);
  if (k >= 9) return "G5";
  if (k == 8) return "G4";
  if (k == 7) return "G3";
  if (k == 6) return "G2";
  if (k == 5) return "G1";
  return "calm";
}

const char *aqiCategory(float aqi) {
  if (isnan(aqi)) return "";
  if (aqi <= 50.0f) return "Good";
  if (aqi <= 100.0f) return "Moderate";
  if (aqi <= 150.0f) return "Sensitive";
  if (aqi <= 200.0f) return "Unhealthy";
  if (aqi <= 300.0f) return "Very unhealthy";
  return "Hazardous";
}
}  // namespace
#endif

void WorldFeedClient::begin(float lat, float lon, const char *place, int kpThreshold) {
  lat_ = lat;
  lon_ = lon;
  place_ = (place != nullptr) ? place : "";
  kpThreshold_ = kpThreshold;
#if USE_WIFI
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
  feeds_.weatherError = "";

  feeds_.quake = QuakeData();
  feeds_.quake.mag = 4.6f;
  feeds_.quake.depthKm = 33.0f;
  feeds_.quake.place = "near Coquimbo, Chile";
  feeds_.quake.ageMin = 12;
  feeds_.quake.count24h = 18;
  feeds_.quakeValid = true;
  feeds_.quakeMs = millis();
  feeds_.quakeError = "";

  feeds_.aurora = AuroraData();
  feeds_.aurora.kp = 5.0f;
  feeds_.aurora.level = "G1";
  feeds_.aurora.verdict = verdictFor(5.0f);
  feeds_.aurora.trend = 1;
  feeds_.auroraValid = true;
  feeds_.auroraMs = millis();
  feeds_.auroraError = "";

  feeds_.air = AirQualityData();
  feeds_.air.usAqi = 42.0f;
  feeds_.air.pm25 = 6.5f;
  feeds_.air.pm10 = 14.0f;
  feeds_.air.ozone = 72.0f;
  feeds_.air.uvIndex = 3.0f;
  feeds_.air.category = "Good";
  feeds_.airValid = true;
  feeds_.airMs = millis();
  feeds_.airError = "";
}

bool WorldFeedClient::poll(WorldFeeds &out) {
#if USE_WIFI
  uint8_t which = turn_ % 4;
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
    case 3:
      if (feeds_.airValid ? airGate_.ready() : startupGate_.ready()) changed = fetchAirQuality();
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
  if (which == "all" || which == "quake") any = fetchQuake() || any;
  if (which == "all" || which == "aurora") any = fetchAurora() || any;
  if (which == "all" || which == "air") any = fetchAirQuality() || any;
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
bool WorldFeedClient::setError(String &slot, const String &message) {
  if (slot == message) return false;
  slot = message;
  return true;
}

void WorldFeedClient::clearError(String &slot) { slot = ""; }

bool WorldFeedClient::fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return setError(feeds_.weatherError, "wifi down");
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
  if (!http.begin(client, url)) return setError(feeds_.weatherError, "begin failed");
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "weather HTTP " + String(code));
    return setError(feeds_.weatherError, "HTTP " + String(code));
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
    return setError(feeds_.weatherError, String("json ") + err.c_str());
  }

  feeds_.weather.tempC = doc["current"]["temperature_2m"] | NAN;
  feeds_.weather.feelsC = doc["current"]["apparent_temperature"] | NAN;
  feeds_.weather.windKt = doc["current"]["wind_speed_10m"] | NAN;
  feeds_.weather.condition = weatherText(doc["current"]["weather_code"] | -1);
  feeds_.weather.hiC = doc["daily"]["temperature_2m_max"][0] | NAN;
  feeds_.weather.loC = doc["daily"]["temperature_2m_min"][0] | NAN;
  feeds_.weatherValid = true;
  feeds_.weatherMs = millis();
  clearError(feeds_.weatherError);
  Logger::info("world", "weather " + String(feeds_.weather.tempC, 1) + "C " + feeds_.weather.condition);
  return true;
}

bool WorldFeedClient::fetchQuake() {
  if (WiFi.status() != WL_CONNECTED) return setError(feeds_.quakeError, "wifi down");
  const char *url = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_day.geojson";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return setError(feeds_.quakeError, "begin failed");
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "quake HTTP " + String(code));
    return setError(feeds_.quakeError, "HTTP " + String(code));
  }

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
    return setError(feeds_.quakeError, String("json ") + err.c_str());
  }

  feeds_.quake.count24h = doc["metadata"]["count"] | -1;
  JsonArray feats = doc["features"].as<JsonArray>();
  if (feats.isNull() || feats.size() == 0) {
    Logger::warn("world", "quake feed empty");
    return setError(feeds_.quakeError, "feed empty");
  }
  JsonObject f0 = feats[0];
  feeds_.quake.mag = f0["properties"]["mag"] | NAN;
  feeds_.quake.place = (const char *)(f0["properties"]["place"] | "");
  feeds_.quake.depthKm = f0["geometry"]["coordinates"][2] | NAN;

  long long tms = f0["properties"]["time"] | 0LL;
  time_t now = time(nullptr);
  if (now > 1600000000L && tms > 0) {
    feeds_.quake.ageMin = (long)((now - (time_t)(tms / 1000)) / 60);
  } else {
    feeds_.quake.ageMin = -1;
  }
  feeds_.quakeValid = true;
  feeds_.quakeMs = millis();
  clearError(feeds_.quakeError);
  Logger::info("world", "quake M" + String(feeds_.quake.mag, 1) + " " + feeds_.quake.place);
  return true;
}

bool WorldFeedClient::fetchAurora() {
  if (WiFi.status() != WL_CONNECTED) return setError(feeds_.auroraError, "wifi down");
  const char *url = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return setError(feeds_.auroraError, "begin failed");
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "aurora HTTP " + String(code));
    return setError(feeds_.auroraError, "HTTP " + String(code));
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Logger::warn("world", String("aurora json ") + err.c_str());
    return setError(feeds_.auroraError, String("json ") + err.c_str());
  }
  JsonArray rows = doc.as<JsonArray>();
  if (rows.isNull() || rows.size() < 2) {
    Logger::warn("world", "aurora feed short");
    return setError(feeds_.auroraError, "feed short");
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
  clearError(feeds_.auroraError);
  Logger::info("world", "aurora Kp " + String(kp, 1) + " " + feeds_.aurora.level);
  return true;
}

bool WorldFeedClient::fetchAirQuality() {
  if (WiFi.status() != WL_CONNECTED) return setError(feeds_.airError, "wifi down");
  String url = String("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=") +
               String(lat_, 4) + "&longitude=" + String(lon_, 4) +
               "&current=us_aqi,pm10,pm2_5,ozone,uv_index&timezone=auto";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return setError(feeds_.airError, "begin failed");
  int code = http.GET();
  if (code != 200) {
    http.end();
    Logger::warn("world", "air HTTP " + String(code));
    return setError(feeds_.airError, "HTTP " + String(code));
  }

  JsonDocument filter;
  filter["current"]["us_aqi"] = true;
  filter["current"]["pm2_5"] = true;
  filter["current"]["pm10"] = true;
  filter["current"]["ozone"] = true;
  filter["current"]["uv_index"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Logger::warn("world", String("air json ") + err.c_str());
    return setError(feeds_.airError, String("json ") + err.c_str());
  }

  feeds_.air.usAqi = doc["current"]["us_aqi"] | NAN;
  feeds_.air.pm25 = doc["current"]["pm2_5"] | NAN;
  feeds_.air.pm10 = doc["current"]["pm10"] | NAN;
  feeds_.air.ozone = doc["current"]["ozone"] | NAN;
  feeds_.air.uvIndex = doc["current"]["uv_index"] | NAN;
  feeds_.air.category = aqiCategory(feeds_.air.usAqi);
  feeds_.airValid = true;
  feeds_.airMs = millis();
  clearError(feeds_.airError);
  Logger::info("world", "air AQI " + String(feeds_.air.usAqi, 0) + " " + feeds_.air.category);
  return true;
}
#endif
