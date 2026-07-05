#ifndef VISION_GUARD_MOCK_CAMERA_MANAGER_H
#define VISION_GUARD_MOCK_CAMERA_MANAGER_H

#include "CameraManager.h"

class MockCameraManager : public CameraManager {
 public:
  void begin(const HardwareProfile &profile) override;
  CameraStatus status() override;
  const char *driverName() const override;

 private:
  unsigned long frameId_ = 0;
};

#endif
