#ifndef FIELDOPS_MOCK_LORA_GATEWAY_H
#define FIELDOPS_MOCK_LORA_GATEWAY_H

#include "LoRaGateway.h"

class MockLoRaGateway : public LoRaGateway {
 public:
  void begin(const HardwareProfile &profile) override;
  bool poll(SensorPacket &packet) override;
  const char *driverName() const override;

 private:
  Throttle cadence_{3000};
  uint8_t nextNode_ = 0;
};

#endif
