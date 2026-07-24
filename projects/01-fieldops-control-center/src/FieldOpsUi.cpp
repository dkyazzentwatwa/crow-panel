#include "FieldOpsUi.h"
#include <CrowPanelShared.h>

void FieldOpsUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name +
                         " screens=Roster,Detail,Alerts,Log");
  // Brings up the panel + touch and paints the first screen. No-op when
  // USE_DISPLAY=0 (the sketch keeps running Serial-only).
  dashboard_.begin();
}

bool FieldOpsUi::tick(FieldOpsUiEvent &event) {
  return dashboard_.tick(event);
}

void FieldOpsUi::renderDashboard(const SensorPacket &packet) {
  if (packet.presenceOnly) {
    Serial.print(F("[screen:roster] presence node="));
    Serial.print(packet.nodeId);
    Serial.print(F(" rssi="));
    Serial.println(packet.rssi);
  } else {
    Serial.print(F("[screen:roster] node="));
    Serial.print(packet.nodeId);
    Serial.print(F(" tempC="));
    Serial.print(packet.temperatureC, 1);
    Serial.print(F(" humidity="));
    Serial.print(packet.humidityPct, 1);
    Serial.print(F(" battery="));
    Serial.print(packet.batteryPct, 1);
    Serial.print(F(" rssi="));
    Serial.println(packet.rssi);
  }
  dashboard_.onPacket(packet);
}

void FieldOpsUi::renderAlert(const String &alert) {
  Serial.print(F("[screen:alerts] "));
  Serial.println(alert);
  dashboard_.onAlert(alert);
}

void FieldOpsUi::renderSummary(const String &summary) {
  Serial.print(F("[screen:summary] "));
  Serial.println(summary);
  dashboard_.onSummary(summary);
}
