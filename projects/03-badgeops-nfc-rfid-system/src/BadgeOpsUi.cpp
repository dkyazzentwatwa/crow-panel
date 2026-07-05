#include "BadgeOpsUi.h"
#include <CrowPanelShared.h>

#if USE_LVGL
#include <lvgl.h>
#endif

void BadgeOpsUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name + " screens=Tap Badge,Access Granted,Access Denied,Enroll Badge,Badge List,Event History,Settings");
#if USE_LVGL
  Logger::warn("ui", "LVGL include enabled; real BadgeOps layout is still TODO.");
#endif
}

void BadgeOpsUi::renderTap(const BadgeRead &read) {
  Serial.print(F("[screen:tap-badge] reader="));
  Serial.print(read.reader);
  Serial.print(F(" uid="));
  Serial.println(read.uid);
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
}
