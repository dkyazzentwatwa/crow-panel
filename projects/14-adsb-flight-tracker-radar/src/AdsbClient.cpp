#include "AdsbClient.h"

#include <math.h>
#include <string.h>

#include "GeoUtils.h"

#if USE_WIFI
#include <ArduinoJson.h>
#include "esp_heap_caps.h"

namespace {
// Route ArduinoJson's allocations to PSRAM: near a busy airport the response
// can hold hundreds of aircraft, and the P4 has tens of MB of PSRAM to spare.
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override {
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
  }
};
}  // namespace
#endif

void AdsbClient::begin(CrowNetworkClient *net, AircraftStore *store,
                       double homeLat, double homeLon, float rangeKm, int radiusNm,
                       const char *apiBase, const char *userAgent,
                       const char *ssid, const char *pass) {
  net_ = net;
  store_ = store;
  homeLat_ = homeLat;
  homeLon_ = homeLon;
  rangeKm_ = rangeKm;
  radiusNm_ = radiusNm;
  apiBase_ = apiBase;
  userAgent_ = userAgent;
  ssid_ = ssid;
  pass_ = pass;
  sourceLabel_ = (strstr(apiBase, "airplanes") != nullptr)  ? "airplanes.live"
                 : (strstr(apiBase, "adsb.fi") != nullptr) ? "adsb.fi"
                                                           : "adsb";
}

void AdsbClient::setPollIntervalMs(uint32_t ms) { pollMs_ = (ms < 1000) ? 1000 : ms; }

uint32_t AdsbClient::taskStackHighWater() const {
#if USE_WIFI
  return taskHandle_ ? (uint32_t)uxTaskGetStackHighWaterMark(taskHandle_) : 0;
#else
  return 0;
#endif
}

bool AdsbClient::pollOnce() {
#if USE_WIFI
  if (net_ == nullptr || store_ == nullptr) return false;
  if (!minGap_.ready()) return false;  // hard floor: never exceed ~1 req/s

  String url = String(apiBase_) + "/point/" + String(homeLat_, 4) + "/" +
               String(homeLon_, 4) + "/" + String(radiusNm_);
  String body;
  int code = net_->httpGet(url, body, userAgent_);
  if (code != 200 || body.length() == 0) {
    Logger::warn("adsb", "fetch failed (code " + String(code) + ")");
    return false;
  }

  static PsramAllocator psram;
  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  f["hex"] = true;
  f["flight"] = true;
  f["lat"] = true;
  f["lon"] = true;
  f["alt_baro"] = true;
  f["alt_geom"] = true;
  f["gs"] = true;
  f["track"] = true;
  f["t"] = true;
  f["category"] = true;

  JsonDocument doc(&psram);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    Logger::error("adsb", String("json parse: ") + err.c_str());
    return false;
  }

  JsonArray arr = doc["ac"].as<JsonArray>();
  if (arr.isNull()) arr = doc["aircraft"].as<JsonArray>();

  uint16_t seen = 0;
  for (JsonObject ac : arr) {
    if (!ac["lat"].is<float>() || !ac["lon"].is<float>()) continue;
    double lat = ac["lat"].as<double>();
    double lon = ac["lon"].as<double>();
    seen++;
    float dist = GeoUtils::haversineKm(homeLat_, homeLon_, lat, lon);
    if (dist > rangeKm_) continue;  // outside the plotted range

    const char *hex = ac["hex"] | "";
    if (hex[0] == '\0') continue;

    Aircraft a;
    memset(&a, 0, sizeof(a));
    strncpy(a.icao, hex, sizeof(a.icao) - 1);
    strncpy(a.callsign, ac["flight"] | "", sizeof(a.callsign) - 1);
    for (int k = (int)strlen(a.callsign) - 1; k >= 0 && a.callsign[k] == ' '; k--) {
      a.callsign[k] = '\0';  // callsigns are space-padded in the feed
    }
    strncpy(a.type, ac["t"] | "", sizeof(a.type) - 1);
    strncpy(a.category, ac["category"] | "", sizeof(a.category) - 1);

    if (ac["alt_baro"].is<int>()) {
      a.altFt = ac["alt_baro"].as<int>();
      a.haveAlt = true;
    } else if (ac["alt_baro"].is<const char *>() &&
               strcmp(ac["alt_baro"].as<const char *>(), "ground") == 0) {
      a.onGround = true;
    } else if (ac["alt_geom"].is<int>()) {
      a.altFt = ac["alt_geom"].as<int>();
      a.haveAlt = true;
    }

    a.lat = lat;
    a.lon = lon;
    a.groundSpeedKt = ac["gs"] | 0.0f;
    if (ac["track"].is<float>()) {
      a.trackDeg = ac["track"].as<float>();
      a.haveTrack = true;
    }
    a.distanceKm = dist;
    a.bearingDeg = GeoUtils::bearingDeg(homeLat_, homeLon_, lat, lon);
    a.lastSeenMs = millis();
    store_->upsert(a);
  }

  store_->commit(sourceLabel_, seen);
  Logger::info("adsb", "updated: " + String(seen) + " aircraft in range");
  return true;
#else
  return false;
#endif
}

#if USE_WIFI
static void adsbTaskTrampoline(void *arg) { static_cast<AdsbClient *>(arg)->taskLoop(); }

void AdsbClient::taskLoop() {
  // Bring Wi-Fi up here, on the task's own core, so a slow or failing esp_hosted
  // link to the C6 can never block or blank the render loop on the other core.
  if (net_) net_->begin(apiBase_, ssid_, pass_);
  for (;;) {
    if (net_) net_->maintain();
    pollOnce();
    uint32_t d = pollMs_;
    if (d < 1000) d = 1000;
    vTaskDelay(pdMS_TO_TICKS(d));
  }
}
#endif

void AdsbClient::startBackgroundTask() {
#if USE_WIFI
  if (taskHandle_ != nullptr) return;
  BaseType_t ok =
      xTaskCreatePinnedToCore(adsbTaskTrampoline, "adsb", 12288, this, 1, &taskHandle_, 0);
  if (ok == pdPASS) {
    Logger::info("adsb", "background poll task started on core 0");
  } else {
    taskHandle_ = nullptr;
    Logger::error("adsb", "poll task create failed; falling back to in-loop polling");
  }
#endif
}
