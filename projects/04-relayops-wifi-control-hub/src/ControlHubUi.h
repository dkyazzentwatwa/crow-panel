#ifndef RELAYOPS_CONTROL_HUB_UI_H
#define RELAYOPS_CONTROL_HUB_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "HubTypes.h"
#include "ControlHubDashboard.h"

// Thin wrapper over ControlHubDashboard, mirroring FieldOpsUi: it logs each
// update to Serial (so the demo reads on camera with no display) and forwards
// it to the dashboard (which no-ops when USE_DISPLAY=0).
class ControlHubUi {
 public:
  void begin();
  void tick();  // call once per loop()
  void renderSensor(const SensorReading &reading);
  void renderDevice(const ControlDevice &device);
  void renderEvent(const String &message);

  // Drains a queued actuator tap for the sketch to act on. Always false with
  // USE_DISPLAY=0 (no touch surface).
  bool takePendingToggle(String &deviceId, bool &desiredOn);

 private:
  ControlHubDashboard dashboard_;
};

#endif
