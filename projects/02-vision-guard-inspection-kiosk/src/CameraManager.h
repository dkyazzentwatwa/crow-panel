#ifndef VISION_GUARD_CAMERA_MANAGER_H
#define VISION_GUARD_CAMERA_MANAGER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>

struct CameraStatus {
  bool online;
  uint16_t width;
  uint16_t height;
  String mode;
  unsigned long frameId;
};

// The honest hardware reality, surfaced on the Live screen in EVERY build so
// the placeholder frame never masquerades as a real capture. On the ESP32-P4
// the camera path is MIPI-CSI via ESP-IDF (esp_video); esp32-camera does not
// ship for the P4 in Arduino core 3.3.x, so there is no Arduino camera driver
// to bind - the UI shows a synthetic status stream, not a photographed frame.
inline const char *cameraStubReason() { return "p4-csi-unavailable-in-arduino"; }
inline const char *cameraHardwareNote() {
  return "MIPI-CSI (esp_video) is ESP-IDF only; no Arduino P4 camera driver";
}

class CameraManager {
 public:
  virtual ~CameraManager() {}
  virtual void begin(const HardwareProfile &profile);
  virtual CameraStatus status();
  virtual const char *driverName() const;

 protected:
  const HardwareProfile *profile_ = nullptr;
};

#endif
