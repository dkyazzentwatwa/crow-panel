#include "CameraManager.h"

#if USE_CAMERA_DRIVER
#include "esp_camera.h"
#endif

void CameraManager::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_CAMERA_DRIVER
  Logger::warn("camera", "esp_camera include enabled, but real pin config must come from the verified Elecrow example.");
#else
  Logger::info("camera", "real camera driver disabled; using base placeholder");
#endif
}

CameraStatus CameraManager::status() {
  return {false, 0, 0, "driver-disabled", 0};
}

const char *CameraManager::driverName() const {
#if USE_CAMERA_DRIVER
  return "esp-camera-placeholder";
#else
  return "camera-disabled";
#endif
}
