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
};

class SensorNode {
 public:
  static SensorPacket makeMock(uint8_t index);
};

#endif
