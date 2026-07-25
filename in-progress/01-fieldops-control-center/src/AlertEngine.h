#ifndef FIELDOPS_ALERT_ENGINE_H
#define FIELDOPS_ALERT_ENGINE_H

#include <Arduino.h>
#include "SensorNode.h"

class AlertEngine {
 public:
  String evaluate(const SensorPacket &packet) const;
};

#endif
