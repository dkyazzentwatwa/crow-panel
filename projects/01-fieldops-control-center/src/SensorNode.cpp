#include "SensorNode.h"
#include <CrowPanelShared.h>

SensorPacket SensorNode::makeMock(uint8_t index) {
  SensorPacket packet;
  packet.nodeId = MockData::nodeId(index);
  packet.temperatureC = MockData::wave(20.0f + index, 8.0f, 15000 + (index * 1000));
  packet.humidityPct = MockData::wave(42.0f, 18.0f, 18000 + (index * 1500));
  // The battery sweep intentionally dips below AlertEngine's 35% threshold
  // (range 24-94%) so the LOW_BATTERY demo path actually fires on camera.
  packet.batteryPct = constrain(94.0f - ((millis() / 7000 + index * 9) % 70), 0.0f, 100.0f);
  // Typical LoRa RSSI spread: about -40 dBm (next to the gateway) down to
  // around -120 dBm (edge of range). The sweep covers -42 to -91.
  packet.rssi = constrain(-42 - (int)((millis() / 3000 + index * 7) % 50), -120, -30);
  packet.motion = ((millis() / 5000 + index) % 3) == 0;
  packet.receivedAtMs = millis();
  return packet;
}

namespace {
// Returns the token before the next comma (from `pos`) and advances `pos`
// past it. Trims surrounding whitespace.
String nextField(const String &line, int &pos) {
  int comma = line.indexOf(',', pos);
  String field = (comma < 0) ? line.substring(pos) : line.substring(pos, comma);
  pos = (comma < 0) ? line.length() : comma + 1;
  field.trim();
  return field;
}
}  // namespace

bool SensorNode::parseCsvFrame(const String &line, SensorPacket &out) {
  String trimmed = line;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return false;
  }

  int pos = 0;
  String kind = nextField(trimmed, pos);

  if (kind == "SENSOR") {
    out.nodeId = nextField(trimmed, pos);
    if (out.nodeId.length() == 0) {
      return false;
    }
    out.temperatureC = nextField(trimmed, pos).toFloat();
    out.humidityPct = nextField(trimmed, pos).toFloat();
    out.batteryPct = nextField(trimmed, pos).toFloat();
    out.motion = nextField(trimmed, pos).toInt() != 0;
    out.rssi = nextField(trimmed, pos).toInt();
    out.presenceOnly = false;
    out.receivedAtMs = millis();
    return true;
  }

  if (kind == "PRESENCE") {
    out.nodeId = nextField(trimmed, pos);
    if (out.nodeId.length() == 0) {
      return false;
    }
    out.rssi = nextField(trimmed, pos).toInt();
    // Remaining field (node type) is informational; ignored for now.
    out.temperatureC = NAN;
    out.humidityPct = NAN;
    out.batteryPct = NAN;
    out.motion = false;
    out.presenceOnly = true;
    out.receivedAtMs = millis();
    return true;
  }

  return false;
}
