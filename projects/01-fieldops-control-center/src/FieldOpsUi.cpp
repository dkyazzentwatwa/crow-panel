#include "FieldOpsUi.h"
#include <CrowPanelShared.h>

#if USE_LVGL
#include <lvgl.h>
#endif

void FieldOpsUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name + " screens=Dashboard,Node Detail,Alerts,AI Summary,Settings");
#if USE_LVGL
  Logger::warn("ui", "LVGL include enabled; real 1024x600 layout is still TODO.");
#endif
}

void FieldOpsUi::renderDashboard(const SensorPacket &packet) {
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

void FieldOpsUi::renderAlert(const String &alert) {
  Serial.print(F("[screen:alerts] "));
  Serial.println(alert);
}

void FieldOpsUi::renderSummary(const String &summary) {
  Serial.print(F("[screen:ai-summary] "));
  Serial.println(summary);
}
