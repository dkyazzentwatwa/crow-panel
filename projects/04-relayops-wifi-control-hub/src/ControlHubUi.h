#ifndef RELAYOPS_CONTROL_HUB_UI_H
#define RELAYOPS_CONTROL_HUB_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <WorldFeed.h>
#include "HubTypes.h"
#include "ControlHubDashboard.h"

// Thin facade over ControlHubDashboard, mirroring FieldOpsUi: it logs each
// update to Serial (so the demo reads on camera with no display) and forwards
// it to the dashboard (which no-ops when USE_DISPLAY=0). tick() returns the
// typed HubUiEvent the dashboard produced this frame; the sketch executes it.
class ControlHubUi {
 public:
  void begin();
  HubUiEvent tick();  // call once per loop(); returns the launched action
  void renderSensor(const SensorReading &reading);
  void renderDevice(const ControlDevice &device);
  void renderEvent(const String &message);
  void renderWorld(const WorldFeeds &feeds);

  // Serial parity helpers used by the `touch` and `screen` commands.
  void printTouch(Print &out) const { dashboard_.printTouch(out); }
  const char *screenName() const { return dashboard_.screenName(); }
  bool selectScreen(const String &name) { return dashboard_.selectScreenByName(name); }
  bool selectSensor(const String &name) { return dashboard_.selectSensorByName(name); }

 private:
  ControlHubDashboard dashboard_;
};

#endif
