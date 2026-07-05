#include "VisionGuardUi.h"
#include <CrowPanelShared.h>

#if USE_LVGL
#include <lvgl.h>
#endif

void VisionGuardUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name + " screens=Live Camera,Scan QR,Checklist,Result,Event History,Settings");
#if USE_LVGL
  Logger::warn("ui", "LVGL include enabled; real kiosk layout is still TODO.");
#endif
}

void VisionGuardUi::renderCameraStatus(const CameraStatus &status) {
  static unsigned long lastPrintMs = 0;
  if (millis() - lastPrintMs < 2000) {
    return;
  }

  lastPrintMs = millis();
  Serial.print(F("[screen:camera] online="));
  Serial.print(status.online ? F("yes") : F("no"));
  Serial.print(F(" size="));
  Serial.print(status.width);
  Serial.print(F("x"));
  Serial.print(status.height);
  Serial.print(F(" mode="));
  Serial.print(status.mode);
  Serial.print(F(" frame="));
  Serial.println(status.frameId);
}

void VisionGuardUi::renderQr(const String &qr) {
  Serial.print(F("[screen:scan-qr] "));
  Serial.println(qr);
}

void VisionGuardUi::renderChecklist(const InspectionResult &result) {
  Serial.print(F("[screen:checklist] "));
  Serial.print(result.checksPassed);
  Serial.print(F("/"));
  Serial.print(result.checksTotal);
  Serial.print(F(" "));
  Serial.println(result.reason);
}

void VisionGuardUi::renderResult(const InspectionResult &result, const String &aiNote) {
  Serial.print(F("[screen:result] qr="));
  Serial.print(result.qr);
  Serial.print(F(" status="));
  Serial.print(result.status);
  Serial.print(F(" ai=\""));
  Serial.print(aiNote);
  Serial.println(F("\""));
}
