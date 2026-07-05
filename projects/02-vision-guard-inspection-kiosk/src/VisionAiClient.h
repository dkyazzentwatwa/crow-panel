#ifndef VISION_GUARD_AI_CLIENT_H
#define VISION_GUARD_AI_CLIENT_H

#include <Arduino.h>
#include <CrowPanelShared.h>
#include "CameraManager.h"

class VisionAiClient {
 public:
  void begin(NetworkClient *network);
  String classify(const String &qr, const CameraStatus &status);

 private:
  NetworkClient *network_ = nullptr;
};

#endif
