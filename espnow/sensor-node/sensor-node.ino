// CrowPanel ESP-NOW sensor node
// ===============================
// Runs on a PLAIN ESP32. Joins the cypher-chat mesh (so it also shows up in
// the chat) and broadcasts a SensorBeacon inside a normal MESH_MSG_DATA packet
// every few seconds. The CrowPanel bridge recognizes the "SN" magic and
// forwards the telemetry to the panel; other chat nodes just see opaque data.
//
// Flash one per sensor - give each a unique NODE_NAME (<= 16 chars).
//
// Build: arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
//          --library espnow/shared-mesh espnow/sensor-node

#include <Arduino.h>
#include <math.h>
#include "MeshManager.h"
#include "Config.h"
#include "SensorBeacon.h"

// --- Per-node config ---
#define NODE_NAME "ATTIC-01"                 // unique, <= 16 chars
#define NODE_PASSPHRASE DEFAULT_PASSPHRASE   // must match your mesh (default "123456")
#define BEACON_INTERVAL_MS 5000
// #define USE_REAL_SENSOR 1                 // wire a DHT/AHT and fill readSensor()

static unsigned long lastBeacon = 0;

// Mock telemetry that sweeps through the alert thresholds so the dashboard
// shows life without any wired sensor.
static void readSensor(SensorBeacon &b) {
#ifdef USE_REAL_SENSOR
  // TODO: read your sensor here and set b.tempC / b.humidityPct / b.batteryPct
  //       / b.motion. Example (DHT22): b.tempC = dht.readTemperature(); ...
  b.tempC = 21.0f;
  b.humidityPct = 45.0f;
  b.batteryPct = 100.0f;
  b.motion = 0;
#else
  float t = millis() / 1000.0f;
  b.tempC = 22.0f + 6.0f * sinf(t / 12.0f);          // crosses the 27C warning
  b.humidityPct = 45.0f + 15.0f * sinf(t / 17.0f);
  b.batteryPct = 90.0f - fmodf(t, 60.0f);            // drains toward LOW_BATTERY
  b.motion = ((int)(t / 5.0f) % 4 == 0) ? 1 : 0;
#endif
}

static void sendBeacon() {
  SensorBeacon b = {};
  b.magic[0] = SENSOR_BEACON_MAGIC0;
  b.magic[1] = SENSOR_BEACON_MAGIC1;
  b.version = SENSOR_BEACON_VERSION;
  strncpy(b.name, NODE_NAME, sizeof(b.name) - 1);
  readSensor(b);

  meshMgr.broadcast(reinterpret_cast<const uint8_t *>(&b), sizeof(b), MESH_MSG_DATA);
  Serial.printf("[node] beacon %s temp=%.1f batt=%.1f motion=%d\n", NODE_NAME, b.tempC,
                b.batteryPct, b.motion);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  if (!meshMgr.begin(NODE_NAME, NODE_PASSPHRASE)) {
    Serial.println("[node] mesh begin FAILED");
  } else {
    Serial.println("[node] mesh up; broadcasting sensor beacons on channel 1");
  }
}

void loop() {
  meshMgr.update();
  if (millis() - lastBeacon >= BEACON_INTERVAL_MS) {
    lastBeacon = millis();
    sendBeacon();
  }
  delay(2);
}
