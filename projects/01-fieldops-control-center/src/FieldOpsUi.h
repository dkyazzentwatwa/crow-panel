#ifndef FIELDOPS_UI_H
#define FIELDOPS_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SensorNode.h"
#include "FieldOpsDashboard.h"

class FieldOpsUi {
 public:
  void begin();
  // Call once per loop(); drives the dashboard + touch when USE_DISPLAY=1,
  // no-op otherwise.
  void tick();
  void renderDashboard(const SensorPacket &packet);
  void renderAlert(const String &alert);
  void renderSummary(const String &summary);

 private:
  FieldOpsDashboard dashboard_;
};

#endif
