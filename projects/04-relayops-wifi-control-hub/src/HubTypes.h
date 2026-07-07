#ifndef RELAYOPS_HUB_TYPES_H
#define RELAYOPS_HUB_TYPES_H

#include <Arduino.h>

// One telemetry frame a remote node POSTs to the hub (POST /sensor), or that
// the mock source / `feed` serial command synthesizes. Mirrors FieldOps'
// SensorPacket so the dashboard reuse is 1:1.
struct SensorReading {
  String nodeId;
  float temperatureC = NAN;   // NAN when the node reports presence only
  float humidityPct = NAN;    // NAN when absent
  float batteryPct = NAN;     // NAN when absent
  int rssi = 0;
  bool motion = false;
  unsigned long receivedAtMs = 0;
  // True for a node we only know is alive (a heartbeat with no telemetry).
  // The dashboard renders these as a presence tile.
  bool presenceOnly = false;
};

// A controllable ESP32 the hub commands over HTTP. Seeded from the static
// RELAYOPS_STATIC_DEVICES table and/or grown at runtime by POST /register.
struct ControlDevice {
  String deviceId;
  String host;                // "192.168.1.50" (no scheme)
  String path = "/gpio";      // request path
  uint8_t pin = 2;            // GPIO on the remote node
  bool state = false;         // last commanded on/off
  bool online = true;
  unsigned long lastSeenMs = 0;
};

#endif
