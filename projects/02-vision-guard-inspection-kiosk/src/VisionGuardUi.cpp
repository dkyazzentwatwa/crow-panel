#include "VisionGuardUi.h"

void VisionGuardUi::begin() {
  const UiTheme &theme = defaultUiTheme();
  Logger::info("ui", String("theme=") + theme.name + " screens=Live Camera,Scan QR,Checklist,Result,Event History,Settings");
#if USE_DISPLAY
  // Single status screen mirroring the Serial output - the full kiosk
  // layout comes after the panel is hardware-verified.
  CrowDisplay::begin(activeHardwareProfile(), "Vision Guard Inspection Kiosk");
#endif
}

void VisionGuardUi::tick() {
#if USE_DISPLAY
  CrowDisplay::tick();
#endif
}

void VisionGuardUi::renderCameraStatus(const CameraStatus &status) {
  if (!printThrottle_.ready()) {
    return;
  }

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
#if USE_DISPLAY
  CrowDisplay::setLine(0, String("camera ") + (status.online ? "online" : "offline") + "  mode " +
                              status.mode + "  frame " + String(status.frameId));
#endif
}

void VisionGuardUi::renderQr(const String &qr) {
  Serial.print(F("[screen:scan-qr] "));
  Serial.println(qr);
#if USE_DISPLAY
  CrowDisplay::setLine(1, "QR: " + qr);
#endif
}

void VisionGuardUi::renderChecklist(const InspectionResult &result) {
  Serial.print(F("[screen:checklist] "));
  Serial.print(result.checksPassed);
  Serial.print(F("/"));
  Serial.print(result.checksTotal);
  Serial.print(F(" "));
  Serial.println(result.reason);
#if USE_DISPLAY
  CrowDisplay::setLine(2, "checks " + String(result.checksPassed) + "/" + String(result.checksTotal) +
                              "  " + result.reason);
#endif
}

void VisionGuardUi::renderResult(const InspectionResult &result, const String &aiNote) {
  Serial.print(F("[screen:result] qr="));
  Serial.print(result.qr);
  Serial.print(F(" status="));
  Serial.print(result.status);
  Serial.print(F(" ai=\""));
  Serial.print(aiNote);
  Serial.println(F("\""));
#if USE_DISPLAY
  CrowDisplay::setLine(3, result.qr + " -> " + result.status);
  CrowDisplay::setLine(4, aiNote);
#endif
}
