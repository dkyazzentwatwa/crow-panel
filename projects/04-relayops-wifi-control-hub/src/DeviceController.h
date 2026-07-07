#ifndef RELAYOPS_DEVICE_CONTROLLER_H
#define RELAYOPS_DEVICE_CONTROLLER_H

#include <Arduino.h>
#include "HubTypes.h"

// Outbound HTTP controller + in-memory registry of the ESP32s the hub can
// command. setPin() sends
//   GET http://<host><path>?pin=<pin>&state=<0|1>
// to the target node. Members are declared unconditionally (like
// CrowNetworkClient); only setPin()'s body changes with USE_WIFI - a mock
// build logs the command and still flips local state so the demo works.
class DeviceController {
 public:
  static const uint8_t kMaxDevices = 8;

  // Seed a device from the static config table. Deduplicated by deviceId.
  // Returns false only when the table is full.
  bool addDevice(const ControlDevice &dev);

  // Runtime self-registration (POST /register). Upserts by deviceId and
  // returns a pointer to the stored device, or nullptr if the table is full.
  ControlDevice *registerDevice(const String &id, const String &host,
                                const String &path, uint8_t pin);

  // Command a device on/off. Updates its cached state on success. Returns
  // false if the device is unknown or the HTTP call fails.
  bool setPin(const String &deviceId, bool on);

  ControlDevice *find(const String &deviceId);
  uint8_t count() const { return count_; }
  const ControlDevice &at(uint8_t i) const { return devices_[i]; }

 private:
  ControlDevice devices_[kMaxDevices];
  uint8_t count_ = 0;
};

#endif
