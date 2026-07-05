#include "MockLoRaGateway.h"

void MockLoRaGateway::begin(const HardwareProfile &profile) {
  profile_ = &profile;
  Logger::info("lora", "mock gateway ready; no radio hardware required");
}

bool MockLoRaGateway::poll(SensorPacket &packet) {
  if (millis() - lastPacketMs_ < 3000) {
    return false;
  }

  lastPacketMs_ = millis();
  packet = SensorNode::makeMock(nextNode_);
  nextNode_ = (nextNode_ + 1) % 4;
  Logger::info("lora", "mock packet from " + packet.nodeId);
  return true;
}

const char *MockLoRaGateway::driverName() const {
  return "mock-lora-gateway";
}
