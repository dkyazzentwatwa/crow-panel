#ifndef VISION_GUARD_UI_H
#define VISION_GUARD_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "CameraManager.h"
#include "InspectionWorkflow.h"

class VisionGuardUi {
 public:
  void begin();
  void renderCameraStatus(const CameraStatus &status);
  void renderQr(const String &qr);
  void renderChecklist(const InspectionResult &result);
  void renderResult(const InspectionResult &result, const String &aiNote);
};

#endif
