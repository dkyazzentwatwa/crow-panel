#ifndef FIELDOPS_LORA_GATEWAY_H
#define FIELDOPS_LORA_GATEWAY_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "SensorNode.h"

class LoRaGateway {
 public:
  virtual ~LoRaGateway() {}
  virtual void begin(const HardwareProfile &profile);
  virtual bool poll(SensorPacket &packet);
  virtual const char *driverName() const;

 protected:
  const HardwareProfile *profile_ = nullptr;
};

#endif
