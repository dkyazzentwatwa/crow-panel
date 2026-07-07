#ifndef VISION_GUARD_UI_H
#define VISION_GUARD_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "CameraManager.h"
#include "InspectionWorkflow.h"

class VisionGuardUi {
 public:
  void begin();
  // Call once per loop(); drives the status screen when USE_DISPLAY=1, no-op otherwise.
  void tick();
  void renderCameraStatus(const CameraStatus &status);
  void renderQr(const String &qr);
  void renderChecklist(const InspectionResult &result);
  void renderResult(const InspectionResult &result, const String &aiNote);

 private:
  Throttle printThrottle_{2000};  // camera status line at most every 2 s
};

#endif
