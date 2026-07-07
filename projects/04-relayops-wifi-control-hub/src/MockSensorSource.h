#ifndef RELAYOPS_MOCK_SENSOR_SOURCE_H
#define RELAYOPS_MOCK_SENSOR_SOURCE_H

#include <Arduino.h>
#include "HubTypes.h"
#include <CrowPanelShared.h>

// Stand-in for real node POSTs when USE_WIFI=0: emits one synthetic sensor
// reading every few seconds so the dashboard is alive on the bench with no
// network. The USE_WIFI=1 build takes readings from SensorServer instead and
// never instantiates this. Mirrors FieldOps' MockLoRaGateway.
class MockSensorSource {
 public:
  void begin();
  bool poll(SensorReading &out);  // true at cadence, fills `out`

 private:
  Throttle cadence_{3000};
  uint8_t next_ = 0;
};

#endif
