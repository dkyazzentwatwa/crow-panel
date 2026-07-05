#include "LoRaGateway.h"

#if USE_LORA_DRIVER
#include <RadioLib.h>
#endif

void LoRaGateway::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_LORA_DRIVER
  Logger::warn("lora", "RadioLib include enabled, but SX1262 initialization still needs verified Elecrow wiring.");
#else
  Logger::info("lora", "real LoRa driver disabled; using base placeholder");
#endif
}

bool LoRaGateway::poll(SensorPacket &packet) {
  (void)packet;
  return false;
}

const char *LoRaGateway::driverName() const {
#if USE_LORA_DRIVER
  return "sx1262-driver-placeholder";
#else
  return "lora-disabled";
#endif
}
