#include "ControlHubUi.h"
#include <CrowPanelShared.h>

void ControlHubUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name +
                         " screens=Dashboard,Sensor Detail,Devices,Settings");
  // Brings up the panel + touch and paints the dashboard chrome. No-op when
  // USE_DISPLAY=0 (the sketch keeps running Serial-only).
  dashboard_.begin();
}

void ControlHubUi::tick() {
  dashboard_.tick();
}

void ControlHubUi::renderSensor(const SensorReading &reading) {
  if (reading.presenceOnly) {
    Serial.print(F("[screen:dashboard] presence node="));
    Serial.print(reading.nodeId);
    Serial.print(F(" rssi="));
    Serial.println(reading.rssi);
  } else {
    Serial.print(F("[screen:dashboard] node="));
    Serial.print(reading.nodeId);
    Serial.print(F(" tempC="));
    Serial.print(reading.temperatureC, 1);
    Serial.print(F(" humidity="));
    Serial.print(reading.humidityPct, 1);
    Serial.print(F(" battery="));
    Serial.print(reading.batteryPct, 1);
    Serial.print(F(" rssi="));
    Serial.println(reading.rssi);
  }
  dashboard_.onSensor(reading);
}

void ControlHubUi::renderDevice(const ControlDevice &device) {
  Serial.print(F("[screen:devices] "));
  Serial.print(device.deviceId);
  Serial.print(F(" -> "));
  Serial.print(device.state ? F("ON") : F("OFF"));
  Serial.print(F(" (GPIO "));
  Serial.print(device.pin);
  Serial.print(F(" @ "));
  Serial.print(device.host);
  Serial.println(F(")"));
  dashboard_.onDevice(device);
}

void ControlHubUi::renderEvent(const String &message) {
  Serial.print(F("[screen:event] "));
  Serial.println(message);
  dashboard_.onEvent(message);
}

bool ControlHubUi::takePendingToggle(String &deviceId, bool &desiredOn) {
  return dashboard_.takePendingToggle(deviceId, desiredOn);
}

void ControlHubUi::renderWorld(const WorldFeeds &feeds) {
  Serial.print(F("[screen:world]"));
  if (feeds.weatherValid) {
    Serial.print(F(" wx="));
    Serial.print(feeds.weather.tempC, 1);
    Serial.print(F("C "));
    Serial.print(feeds.weather.condition);
  }
  if (feeds.quakeValid) {
    Serial.print(F(" | quake=M"));
    Serial.print(feeds.quake.mag, 1);
    Serial.print(F(" "));
    Serial.print(feeds.quake.place);
  }
  if (feeds.auroraValid) {
    Serial.print(F(" | Kp="));
    Serial.print(feeds.aurora.kp, 1);
    Serial.print(F(" "));
    Serial.print(feeds.aurora.level);
  }
  Serial.println();
  dashboard_.onWorldFeeds(feeds);
}
