#include "MockSensorSource.h"

void MockSensorSource::begin() {
  Logger::info("mock", "synthetic sensor source ready; no network required");
}

bool MockSensorSource::poll(SensorReading &out) {
  if (!cadence_.ready()) {
    return false;
  }

  out.nodeId = MockData::nodeId(next_);
  out.temperatureC = MockData::wave(20.0f + next_, 8.0f, 15000 + (next_ * 1000));
  out.humidityPct = MockData::wave(42.0f, 18.0f, 18000 + (next_ * 1500));
  // Sweep dips low so a LOW_BATTERY-style demo reads convincingly on the tile.
  out.batteryPct = constrain(94.0f - ((millis() / 7000 + next_ * 9) % 70), 0.0f, 100.0f);
  out.rssi = constrain(-42 - (int)((millis() / 3000 + next_ * 7) % 50), -120, -30);
  out.motion = ((millis() / 5000 + next_) % 3) == 0;
  out.presenceOnly = false;
  out.receivedAtMs = millis();

  next_ = (next_ + 1) % 3;
  Logger::info("mock", "reading from " + out.nodeId);
  return true;
}
