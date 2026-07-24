#ifndef VISION_GUARD_AI_CLIENT_H
#define VISION_GUARD_AI_CLIENT_H

#include <Arduino.h>
#include <CrowPanelShared.h>
#include "CameraManager.h"

// Mock vision-classifier front end. When Wi-Fi is built in AND connected it
// POSTs the prompt to the summary endpoint; otherwise it returns a believable,
// outcome-aware note locally so the Result screen always has something honest
// to show. It never claims to have seen a real camera frame.
class VisionAiClient {
 public:
  void begin(CrowNetworkClient *network);

  // `pass` and `concern` let the note reflect the checklist outcome (concern
  // is the failing item's name, or nullptr when the run passed).
  String classify(const String &qr, const CameraStatus &status,
                  bool pass = true, const char *concern = nullptr);

 private:
  CrowNetworkClient *network_ = nullptr;
  uint32_t noteRotation_ = 0;
};

#endif
