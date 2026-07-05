#include "SensorNode.h"
#include <CrowPanelShared.h>

SensorPacket SensorNode::makeMock(uint8_t index) {
  SensorPacket packet;
  packet.nodeId = MockData::nodeId(index);
  packet.temperatureC = MockData::wave(20.0f + index, 8.0f, 15000 + (index * 1000));
  packet.humidityPct = MockData::wave(42.0f, 18.0f, 18000 + (index * 1500));
  packet.batteryPct = 94.0f - ((millis() / 7000 + index * 9) % 55);
  packet.rssi = -42 - ((millis() / 3000 + index * 7) % 35);
  packet.motion = ((millis() / 5000 + index) % 3) == 0;
  packet.receivedAtMs = millis();
  return packet;
}
