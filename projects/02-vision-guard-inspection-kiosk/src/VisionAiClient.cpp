#include "VisionAiClient.h"

void VisionAiClient::begin(NetworkClient *network) {
  network_ = network;
  Logger::info("vision-ai", "mock AI vision client ready");
}

String VisionAiClient::classify(const String &qr, const CameraStatus &status) {
  String prompt = "Classify " + qr + " frame=" + String(status.frameId) + " mode=" + status.mode;
  if (network_ != nullptr) {
    return network_->postSummaryRequest(prompt);
  }

  return "Mock vision: item label and workspace look acceptable.";
}
