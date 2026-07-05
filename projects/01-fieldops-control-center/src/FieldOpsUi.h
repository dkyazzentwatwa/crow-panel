#ifndef FIELDOPS_UI_H
#define FIELDOPS_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SensorNode.h"

class FieldOpsUi {
 public:
  void begin();
  void renderDashboard(const SensorPacket &packet);
  void renderAlert(const String &alert);
  void renderSummary(const String &summary);
};

#endif
