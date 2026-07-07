#pragma once

#include <Arduino.h>

// Sensor telemetry carried inside a cypher-chat MESH_MSG_DATA payload. The
// magic + version let the CrowPanel bridge pick sensor readings out of
// ordinary mesh/chat traffic; to every other cypher-chat node it's just an
// opaque data message. ~32 bytes, well under MESH_MAX_PAYLOAD (200).
#define SENSOR_BEACON_MAGIC0 'S'
#define SENSOR_BEACON_MAGIC1 'N'
#define SENSOR_BEACON_VERSION 1

struct __attribute__((packed)) SensorBeacon {
  char magic[2];        // {'S','N'}
  uint8_t version;      // SENSOR_BEACON_VERSION
  char name[16];        // node short name, null-padded
  float tempC;
  float humidityPct;
  float batteryPct;
  uint8_t motion;       // 0/1
};

inline bool sensorBeaconValid(const uint8_t *payload, uint8_t len) {
  if (len < sizeof(SensorBeacon)) return false;
  const SensorBeacon *b = reinterpret_cast<const SensorBeacon *>(payload);
  return b->magic[0] == SENSOR_BEACON_MAGIC0 &&
         b->magic[1] == SENSOR_BEACON_MAGIC1 &&
         b->version == SENSOR_BEACON_VERSION;
}
