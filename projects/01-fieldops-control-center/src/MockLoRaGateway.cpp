#include "MockLoRaGateway.h"

void MockLoRaGateway::begin(const HardwareProfile &profile) {
  profile_ = &profile;
  Logger::info("lora", "mock gateway ready; no radio hardware required");
}

bool MockLoRaGateway::poll(SensorPacket &packet) {
  if (!cadence_.ready()) {
    return false;
  }

  packet = SensorNode::makeMock(nextNode_);
  nextNode_ = (nextNode_ + 1) % 4;
  Logger::info("lora", "mock packet from " + packet.nodeId);
  return true;
}

const char *MockLoRaGateway::driverName() const {
  return "mock-lora-gateway";
}
