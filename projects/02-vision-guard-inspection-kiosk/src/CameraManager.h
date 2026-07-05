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
