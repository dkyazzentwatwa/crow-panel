#include "CameraManager.h"

// The ESP32-P4 camera path is MIPI-CSI via ESP-IDF (esp_video); the
// esp32-camera library does not ship for the P4 in Arduino core 3.3.x
// (verified: absent from esp32p4-libs). __has_include keeps this file
// compiling either way, so the flag matrix stays green while being honest
// about the gap. The only official example is Elecrow's IDF lesson:
// example/V1.0/idf-code/Lesson13-Camera_Real-Time. Arduino support means
// waiting for the core to bundle a P4 camera driver or porting that lesson.
#if USE_CAMERA_DRIVER && __has_include("esp_camera.h")
#include "esp_camera.h"
#define VISION_GUARD_CAMERA_AVAILABLE 1
#else
#define VISION_GUARD_CAMERA_AVAILABLE 0
#endif

#if USE_CAMERA_DRIVER && !VISION_GUARD_CAMERA_AVAILABLE
#warning "USE_CAMERA_DRIVER=1 but esp_camera.h is unavailable on this target (ESP32-P4: MIPI-CSI is ESP-IDF only). Building the honest stub."
#endif

void CameraManager::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_CAMERA_DRIVER && VISION_GUARD_CAMERA_AVAILABLE
  Logger::warn("camera", "esp_camera available, but init still needs the verified Elecrow pin config before power-on.");
#elif USE_CAMERA_DRIVER
  Logger::warn("camera", "esp32-camera is not available for the ESP32-P4 in this Arduino core; see the project README.");
#else
  Logger::info("camera", "real camera driver disabled; using base placeholder");
#endif
}

CameraStatus CameraManager::status() {
#if USE_CAMERA_DRIVER && !VISION_GUARD_CAMERA_AVAILABLE
  return {false, 0, 0, "p4-csi-unavailable-in-arduino", 0};
#else
  return {false, 0, 0, "driver-disabled", 0};
#endif
}

const char *CameraManager::driverName() const {
#if USE_CAMERA_DRIVER && VISION_GUARD_CAMERA_AVAILABLE
  return "esp-camera-placeholder";
#elif USE_CAMERA_DRIVER
  return "p4-csi-unavailable-in-arduino";
#else
  return "camera-disabled";
#endif
}
