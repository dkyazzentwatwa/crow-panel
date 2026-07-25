#include "AiSummaryClient.h"

void AiSummaryClient::begin(CrowNetworkClient *network) {
  network_ = network;
  Logger::info("ai", "mock summary client ready");
}

String AiSummaryClient::summarize(const SensorPacket &packet, const String &alert) {
  String prompt = "Node " + packet.nodeId + " temp=" + String(packet.temperatureC, 1) + " battery=" + String(packet.batteryPct, 1);
  if (alert.length() > 0) {
    prompt += " alert=" + alert;
  }

  if (network_ != nullptr) {
    return network_->postSummaryRequest(prompt);
  }

  return "Mock summary: " + packet.nodeId + " is within normal demo range.";
}
