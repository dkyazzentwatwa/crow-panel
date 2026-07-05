#include "MockCameraManager.h"

void MockCameraManager::begin(const HardwareProfile &profile) {
  profile_ = &profile;
  Logger::info("camera", "mock camera ready; no camera hardware required");
}

CameraStatus MockCameraManager::status() {
  frameId_++;
  CameraStatus status;
  status.online = true;
  status.width = 1024;
  status.height = 600;
  status.mode = (millis() / 8000) % 2 == 0 ? "live-preview" : "inspection-freeze";
  status.frameId = frameId_;
  return status;
}

const char *MockCameraManager::driverName() const {
  return "mock-camera";
}
