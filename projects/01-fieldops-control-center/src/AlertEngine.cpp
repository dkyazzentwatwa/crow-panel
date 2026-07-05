#include "AlertEngine.h"

String AlertEngine::evaluate(const SensorPacket &packet) const {
  if (packet.batteryPct < 35.0f) {
    return "LOW_BATTERY " + packet.nodeId + " battery=" + String(packet.batteryPct, 1) + "%";
  }

  if (packet.temperatureC > 27.0f) {
    return "TEMP_WARNING " + packet.nodeId + " temp=" + String(packet.temperatureC, 1) + "C";
  }

  if (packet.motion) {
    return "MOTION_EVENT " + packet.nodeId + " remote enclosure opened";
  }

  return "";
}
