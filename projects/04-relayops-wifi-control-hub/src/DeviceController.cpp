// Outbound GPIO control: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8),
// NOT HARDWARE-VERIFIED. HTTP rides the onboard ESP32-C6 (esp_hosted); the
// remote nodes are ordinary ESP32s serving a tiny /gpio handler. In a mock
// build (USE_WIFI=0) setPin() logs the command instead of sending it.

#include "DeviceController.h"
#include <CrowPanelShared.h>

#if USE_WIFI
#include <HTTPClient.h>
#endif

bool DeviceController::addDevice(const ControlDevice &dev) {
  if (ControlDevice *existing = find(dev.deviceId)) {
    *existing = dev;  // config wins on a duplicate id
    return true;
  }
  if (count_ >= kMaxDevices) {
    Logger::warn("devices", "registry full, ignoring " + dev.deviceId);
    return false;
  }
  devices_[count_++] = dev;
  return true;
}

ControlDevice *DeviceController::registerDevice(const String &id, const String &host,
                                                const String &path, uint8_t pin) {
  ControlDevice *dev = find(id);
  if (dev == nullptr) {
    if (count_ >= kMaxDevices) {
      Logger::warn("devices", "registry full, cannot register " + id);
      return nullptr;
    }
    dev = &devices_[count_++];
    dev->deviceId = id;
    dev->state = false;
  }
  dev->host = host;
  dev->path = path.length() > 0 ? path : "/gpio";
  dev->pin = pin;
  dev->online = true;
  dev->lastSeenMs = millis();
  Logger::info("devices", "registered " + id + " -> " + dev->host + dev->path);
  return dev;
}

ControlDevice *DeviceController::find(const String &deviceId) {
  for (uint8_t i = 0; i < count_; i++) {
    if (devices_[i].deviceId == deviceId) return &devices_[i];
  }
  return nullptr;
}

bool DeviceController::setPin(const String &deviceId, bool on) {
  ControlDevice *dev = find(deviceId);
  if (dev == nullptr) {
    Logger::warn("devices", "unknown device " + deviceId);
    return false;
  }

  String url = "http://" + dev->host + dev->path + "?pin=" + String(dev->pin) +
               "&state=" + (on ? "1" : "0");

#if USE_WIFI
  {
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.begin(url);
    int code = http.GET();
    http.end();
    if (code > 0 && code >= 200 && code < 300) {
      dev->state = on;
      dev->online = true;
      dev->lastSeenMs = millis();
      Logger::info("devices", "GET " + url + " -> " + String(code));
      return true;
    }
    dev->online = (code > 0);
    Logger::error("devices", "GET " + url + " failed: " +
                                 (code > 0 ? String(code) : HTTPClient::errorToString(code)));
    return false;
  }
#else
  // Mock build: no radio. Log the command and flip local state so the
  // dashboard + `set` demo behave exactly as they will with USE_WIFI=1.
  dev->state = on;
  dev->lastSeenMs = millis();
  Logger::info("devices", "GET " + url + " (mock; log-only)");
  return true;
#endif
}
