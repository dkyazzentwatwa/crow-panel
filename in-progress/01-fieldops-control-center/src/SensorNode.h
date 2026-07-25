#ifndef FIELDOPS_SENSOR_NODE_H
#define FIELDOPS_SENSOR_NODE_H

#include <Arduino.h>

struct SensorPacket {
  String nodeId;
  float temperatureC;
  float humidityPct;
  float batteryPct;
  int rssi;
  bool motion;
  unsigned long receivedAtMs;
  // True for a node we only know is alive (e.g. a cypher-chat chat node's
  // heartbeat) with no telemetry. The pipeline skips alerts/summary for these
  // and the dashboard renders them as a presence tile.
  bool presenceOnly = false;
};

class SensorNode {
 public:
  static SensorPacket makeMock(uint8_t index);

  // Parse one CSV frame from the ESP-NOW bridge into `out`. Formats:
  //   SENSOR,<name>,<tempC>,<hum>,<batt>,<motion0/1>,<rssi>
  //   PRESENCE,<name>,<rssi>,<type>
  // Returns false on an unrecognized or malformed line. Used by both
  // EspNowGateway (Serial1) and the `feed` serial command.
  static bool parseCsvFrame(const String &line, SensorPacket &out);
};

#endif
