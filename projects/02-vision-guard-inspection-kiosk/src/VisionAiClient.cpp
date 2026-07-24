#include "VisionAiClient.h"

void VisionAiClient::begin(CrowNetworkClient *network) {
  network_ = network;
  Logger::info("vision-ai", "mock AI vision client ready");
}

String VisionAiClient::classify(const String &qr, const CameraStatus &status,
                                bool pass, const char *concern) {
  String prompt = "Classify " + qr + " frame=" + String(status.frameId) + " mode=" + status.mode +
                  (pass ? " verdict=ok" : String(" verdict=defect:") + (concern ? concern : "unknown"));

  // Prefer the backend only when it is actually reachable; the mock fallback
  // there is generic, so offline builds compose their own varied note instead.
  if (network_ != nullptr && network_->connected()) {
    String body = network_->postSummaryRequest(prompt);
    if (body.length() > 0) return body;
  }
  Logger::info("vision-ai", "local classify " + prompt);

  if (!pass && concern != nullptr) {
    return String("Vision: ") + concern + " reads out of spec; recommend a manual recheck.";
  }
  static const char *kOkNotes[] = {
    "Vision: item label and workspace look within tolerance.",
    "Vision: print is crisp and the fixture is seated correctly.",
    "Vision: no anomalies in framing; surface looks clean.",
    "Vision: barcode contrast is strong; alignment nominal.",
  };
  const char *note = kOkNotes[noteRotation_ % (sizeof(kOkNotes) / sizeof(kOkNotes[0]))];
  noteRotation_++;
  return String(note);
}
