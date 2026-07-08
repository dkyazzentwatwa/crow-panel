#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/AdsbTypes.h"
#include "src/GeoUtils.h"
#include "src/AircraftStore.h"
#include "src/MockAdsbSource.h"
#include "src/AdsbClient.h"
#include "src/RadarUi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Project 14 - ADS-B Flight Tracker Radar Dashboard.
// A sweep-radar scope + distance-sorted aircraft list on the 1024x600 panel, fed
// by the keyless airplanes.live / adsb.fi point API over Wi-Fi (background fetch
// task on core 0). The default build is fully offline: MockAdsbSource synthesizes
// aircraft so the radar and list are alive with no network. Arduino_GFX only.
AircraftStore store;
RadarUi ui;
SerialCommandRouter router;
CrowNetworkClient network;
WorldFeedClient world;
WorldFeeds worldFeeds;
SemaphoreHandle_t worldMutex = nullptr;
SemaphoreHandle_t worldClientMutex = nullptr;
volatile bool worldDirty = false;
Throttle worldUiGate{1000};
#if USE_WIFI
AdsbClient adsb;
TaskHandle_t worldTaskHandle = nullptr;
#else
MockAdsbSource mockSource;
#endif

AdsbSnapshot g_snap;  // scratch snapshot for serial dumps (not the render copy)
uint32_t g_pollMs = ADSB_POLL_INTERVAL_MS;

// --- Serial commands (defined before use: the ctags workaround skips prototype
// generation, so order matters). ---
void publishWorldFeeds(const WorldFeeds &feeds) {
  if (worldMutex != nullptr) xSemaphoreTake(worldMutex, portMAX_DELAY);
  worldFeeds = feeds;
  worldDirty = true;
  if (worldMutex != nullptr) xSemaphoreGive(worldMutex);
}

bool copyWorldFeeds(WorldFeeds &out) {
  bool dirty = false;
  if (worldMutex != nullptr) xSemaphoreTake(worldMutex, portMAX_DELAY);
  out = worldFeeds;
  dirty = worldDirty;
  worldDirty = false;
  if (worldMutex != nullptr) xSemaphoreGive(worldMutex);
  return dirty;
}

bool pollWorldFeeds(WorldFeeds &out) {
  if (worldClientMutex != nullptr) xSemaphoreTake(worldClientMutex, portMAX_DELAY);
  bool changed = world.poll(out);
  if (worldClientMutex != nullptr) xSemaphoreGive(worldClientMutex);
  return changed;
}

bool refreshWorldFeeds(WorldFeeds &out, const String &which) {
  if (worldClientMutex != nullptr) xSemaphoreTake(worldClientMutex, portMAX_DELAY);
  bool changed = world.refresh(out, which);
  if (worldClientMutex != nullptr) xSemaphoreGive(worldClientMutex);
  return changed;
}

void printWorldSummary(const WorldFeeds &feeds) {
  Serial.print(F("[world]"));
  if (feeds.weatherValid) {
    Serial.printf(" wx=%.1fC %s", feeds.weather.tempC, feeds.weather.condition.c_str());
  } else if (feeds.weatherError.length()) {
    Serial.print(F(" wx_error="));
    Serial.print(feeds.weatherError);
  }
  if (feeds.quakeValid) {
    Serial.printf(" | quake=M%.1f %s", feeds.quake.mag, feeds.quake.place.c_str());
  } else if (feeds.quakeError.length()) {
    Serial.print(F(" | quake_error="));
    Serial.print(feeds.quakeError);
  }
  if (feeds.auroraValid) {
    Serial.printf(" | Kp=%.1f %s", feeds.aurora.kp, feeds.aurora.level.c_str());
  } else if (feeds.auroraError.length()) {
    Serial.print(F(" | aurora_error="));
    Serial.print(feeds.auroraError);
  }
  if (feeds.airValid) {
    Serial.printf(" | AQI=%.0f %s", feeds.air.usAqi, feeds.air.category.c_str());
  } else if (feeds.airError.length()) {
    Serial.print(F(" | air_error="));
    Serial.print(feeds.airError);
  }
  Serial.println();
}

#if USE_WIFI
void worldTaskLoop(void *) {
  WorldFeeds latest;
  for (;;) {
    if (pollWorldFeeds(latest)) {
      publishWorldFeeds(latest);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void startWorldTask() {
  if (worldTaskHandle != nullptr) return;
  BaseType_t ok = xTaskCreatePinnedToCore(worldTaskLoop, "world", 12288, nullptr, 1,
                                          &worldTaskHandle, 0);
  if (ok == pdPASS) {
    Logger::info("world", "background feed task started on core 0");
  } else {
    worldTaskHandle = nullptr;
    Logger::error("world", "feed task create failed; world screens update in-loop only");
  }
}
#endif

void cmdStatus(const String &) {
  printSystemStatus(Serial, "adsb-radar", store.count());
  store.copySnapshot(g_snap);
  Serial.printf("[status] screen=%s source=%s contacts=%u seen=%u range=%ukm poll=%lums\n",
                ui.screenName(),
                g_snap.source, (unsigned)g_snap.count, (unsigned)g_snap.totalSeen,
                (unsigned)ui.rangeKm(), (unsigned long)g_pollMs);
#if USE_WIFI
  Serial.printf("[status] wifi=%s adsb_task_stack_hw=%lu words",
                network.connected() ? "connected" : "down",
                (unsigned long)adsb.taskStackHighWater());
  if (worldTaskHandle != nullptr) {
    Serial.printf(" world_task_stack_hw=%lu words",
                  (unsigned long)uxTaskGetStackHighWaterMark(worldTaskHandle));
  }
  Serial.println();
#endif
  WorldFeeds feeds;
  copyWorldFeeds(feeds);
  printWorldSummary(feeds);
}

void cmdPlanes(const String &) {
  store.copySnapshot(g_snap);
  Serial.printf("[planes] %u contacts (nearest first):\n", (unsigned)g_snap.count);
  for (uint8_t i = 0; i < g_snap.count; i++) {
    const Aircraft &a = g_snap.ac[i];
    const char *call = (a.callsign[0] != '\0') ? a.callsign : "UNKNOWN";
    Serial.printf("  %-8s %-6s %6.1fkm brg %3.0f  ", call, a.icao, a.distanceKm, a.bearingDeg);
    if (a.haveAlt) {
      Serial.printf("%6ldft ", (long)a.altFt);
    } else {
      Serial.print("   ---ft ");
    }
    Serial.printf("%4.0fkt %-4s\n", a.groundSpeedKt, a.type[0] ? a.type : "-");
  }
}

void cmdRange(const String &args) {
  int km = args.toInt();
  if (km < 5 || km > 400) {
    Logger::warn("cmd", "usage: range <km> (5-400); rings are drawn at km/5 steps");
    return;
  }
  ui.setRangeKm((uint16_t)km);
  Logger::info("cmd", "display range = " + String(km) + " km");
}

void cmdPoll(const String &args) {
  int sec = args.toInt();
  if (sec < 1 || sec > 120) {
    Logger::warn("cmd", "usage: poll <sec> (1-120); the live feed is 1 req/s max");
    return;
  }
  g_pollMs = (uint32_t)sec * 1000UL;
#if USE_WIFI
  adsb.setPollIntervalMs(g_pollMs);
#endif
  Logger::info("cmd", "poll interval = " + String(sec) + " s");
}

void cmdMock(const String &) {
  store.copySnapshot(g_snap);
  Serial.printf("[mock] source=%s (USE_WIFI=%d): %s\n", g_snap.source, USE_WIFI,
                USE_WIFI ? "live feed active" : "synthetic aircraft (no network)");
}

void cmdScreen(const String &args) {
  String target = args;
  target.trim();
  if (target.length() == 0) {
    Serial.println(F("[screen] use: screen next|radar|weather|quake|aurora|air"));
    Serial.printf("[screen] current=%s\n", ui.screenName());
    return;
  }
  if (!ui.setScreen(target)) {
    Logger::warn("cmd", "unknown screen " + target + " (use next|radar|weather|quake|aurora|air)");
    return;
  }
  Logger::info("cmd", String("screen = ") + ui.screenName());
}

void cmdWorld(const String &args) {
  String which = args;
  which.trim();
  which.toLowerCase();
  if (which.length() == 0) which = "all";
  if (!(which == "all" || which == "weather" || which == "wx" || which == "quake" ||
        which == "quakes" || which == "earthquake" || which == "earthquakes" ||
        which == "aurora" || which == "kp" || which == "air" || which == "aqi")) {
    Serial.println(F("[world] use: world [all|weather|quake|aurora|air]"));
    return;
  }
  if (which == "wx") which = "weather";
  if (which == "quakes" || which == "earthquake" || which == "earthquakes") which = "quake";
  if (which == "kp") which = "aurora";
  if (which == "aqi") which = "air";

  WorldFeeds latest;
  bool changed = refreshWorldFeeds(latest, which);
  publishWorldFeeds(latest);
  ui.setWorldFeeds(latest);
  printWorldSummary(latest);
  Logger::info("cmd", String("world refresh ") + which + (changed ? " updated" : " no-change"));
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel ADS-B Flight Tracker Radar");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  store.begin();
  worldMutex = xSemaphoreCreateMutex();
  worldClientMutex = xSemaphoreCreateMutex();

  // Bring the display up FIRST so the radar always renders, even if Wi-Fi
  // bring-up is slow or unavailable: Wi-Fi rides the onboard ESP32-C6 over
  // esp_hosted and can block, and it must never keep the panel dark.
  ui.begin((uint16_t)ADSB_RANGE_KM);
  world.begin(ADSB_HOME_LAT, ADSB_HOME_LON, ADSB_SITE_NAME, ADSB_WORLD_KP_THRESHOLD);

#if USE_WIFI
  adsb.begin(&network, &store, ADSB_HOME_LAT, ADSB_HOME_LON, (float)ADSB_RANGE_KM,
             ADSB_RANGE_NM, ADSB_API_BASE, ADSB_USER_AGENT, WIFI_SSID, WIFI_PASS);
  adsb.setPollIntervalMs(g_pollMs);
#if ADSB_POLL_TASK
  // The poll task brings Wi-Fi up on its own core, so a slow/failing C6 link
  // can never block the render loop or reboot-loop the panel.
  adsb.startBackgroundTask();
#else
  network.begin(ADSB_API_BASE, WIFI_SSID, WIFI_PASS);
#endif
  startWorldTask();
#else
  mockSource.begin(ADSB_HOME_LAT, ADSB_HOME_LON, (float)ADSB_RANGE_KM);
  WorldFeeds mockFeeds;
  if (pollWorldFeeds(mockFeeds)) {
    publishWorldFeeds(mockFeeds);
    ui.setWorldFeeds(mockFeeds);
  }
#endif

  router.begin(Serial, "adsb-radar");
  router.on("status", "uptime, heap, flags, contact count, range", cmdStatus);
  router.on("planes", "list current aircraft, nearest first", cmdPlanes);
  router.on("range", "range <km> - set the outer radar ring (5-400)", cmdRange);
  router.on("poll", "poll <sec> - live fetch interval (1-120)", cmdPoll);
  router.on("mock", "report the active data source", cmdMock);
  router.on("screen", "screen next|radar|weather|quake|aurora|air", cmdScreen);
  router.on("world", "world [all|weather|quake|aurora|air] - refresh public feeds", cmdWorld);
}

void loop() {
  router.poll();
#if !(USE_WIFI && ADSB_POLL_TASK)
  network.maintain();  // in the task-poller build the task owns the Wi-Fi lifecycle
#endif

#if USE_WIFI
#if !ADSB_POLL_TASK
  // In-loop fallback poller (ADSB_POLL_TASK=0): fetch at the configured cadence.
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll >= g_pollMs) {
    lastPoll = millis();
    adsb.pollOnce();
  }
#endif
#else
  mockSource.tick(store);
  WorldFeeds mockFeeds;
  if (pollWorldFeeds(mockFeeds)) {
    publishWorldFeeds(mockFeeds);
    ui.setWorldFeeds(mockFeeds);
  }
#endif

#if USE_WIFI
  if (worldTaskHandle == nullptr) {
    WorldFeeds latest;
    if (pollWorldFeeds(latest)) {
      publishWorldFeeds(latest);
    }
  }
#endif

  WorldFeeds feeds;
  bool feedChanged = copyWorldFeeds(feeds);
  if (feedChanged) {
    ui.setWorldFeeds(feeds);
  }
  ui.tick(store);

  delay(USE_DISPLAY ? 5 : 20);  // tighter loop when animating the sweep
}
