#include "BadgeOpsUi.h"
#include <CrowPanelShared.h>

void BadgeOpsUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name + " screens=Tap Badge,Access Granted,Access Denied,Enroll Badge,Badge List,Event History,Settings");
#if USE_DISPLAY
  // Single status screen mirroring the Serial output - the full kiosk
  // layout comes after the panel is hardware-verified.
  CrowDisplay::begin(activeHardwareProfile(), "BadgeOps Access Terminal");
#endif
}

void BadgeOpsUi::tick() {
#if USE_DISPLAY
  CrowDisplay::tick();
#endif
}

void BadgeOpsUi::renderTap(const BadgeRead &read) {
  Serial.print(F("[screen:tap-badge] reader="));
  Serial.print(read.reader);
  Serial.print(F(" uid="));
  Serial.println(read.uid);
#if USE_DISPLAY
  CrowDisplay::setLine(0, "tap " + read.uid + " via " + read.reader);
#endif
}

void BadgeOpsUi::renderDecision(const AccessDecision &decision, const BadgeRecord &record, bool found) {
  Serial.print(F("[screen:access-"));
  Serial.print(decision.status);
  Serial.print(F("] "));
  Serial.print(decision.message);
  if (found) {
    Serial.print(F(" role="));
    Serial.print(record.role);
  }
  Serial.println();
#if USE_DISPLAY
  CrowDisplay::setLine(1, decision.message);
  CrowDisplay::setLine(2, found ? (record.name + " (" + record.role + ")") : String("unknown badge"));
#endif
}
