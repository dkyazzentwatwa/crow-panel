#ifndef FIELDOPS_ESPNOW_GATEWAY_H
#define FIELDOPS_ESPNOW_GATEWAY_H

#include "LoRaGateway.h"

// ESP-NOW transport for the panel. The ESP32-P4 cannot join ESP-NOW directly
// (WiFi is remote on the C6), so a plain ESP32 runs the radio and bridges
// mesh traffic to us over UART as CSV frames (see espnow/README.md). This
// gateway reads Serial1 non-blocking and parses one frame per poll() via
// SensorNode::parseCsvFrame - it satisfies the same LoRaGateway contract as
// the mock and SX1262 transports, so the rest of the pipeline is unchanged.
class EspNowGateway : public LoRaGateway {
 public:
  void begin(const HardwareProfile &profile) override;
  bool poll(SensorPacket &packet) override;
  const char *driverName() const override;

 private:
  static const uint8_t kMaxLine = 128;
  char line_[kMaxLine];
  uint8_t lineLen_ = 0;
};

#endif
