#include "FieldOpsUi.h"
#include <CrowPanelShared.h>

void FieldOpsUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name + " screens=Dashboard,Node Detail,Alerts,AI Summary,Settings");
  // Brings up the panel + touch and paints the dashboard chrome. No-op when
  // USE_DISPLAY=0 (the sketch keeps running Serial-only).
  dashboard_.begin();
}

void FieldOpsUi::tick() {
  dashboard_.tick();
}

void FieldOpsUi::renderDashboard(const SensorPacket &packet) {
  if (packet.presenceOnly) {
    Serial.print(F("[screen:dashboard] presence node="));
    Serial.print(packet.nodeId);
    Serial.print(F(" rssi="));
    Serial.println(packet.rssi);
  } else {
    Serial.print(F("[screen:dashboard] node="));
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
  Serial.print(F("[screen:ai-summary] "));
  Serial.println(summary);
  dashboard_.onSummary(summary);
}
