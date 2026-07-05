#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockCameraManager.h"
#include "src/QrScanner.h"
#include "src/InspectionWorkflow.h"
#include "src/VisionAiClient.h"
#include "src/VisionGuardUi.h"
#include "src/EventLog.h"

MockCameraManager camera;
QrScanner qrScanner;
InspectionWorkflow workflow;
VisionAiClient visionAi;
VisionGuardUi ui;
EventLog eventLog;
StorageManager storage;
NetworkClient network;

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Vision Guard Inspection Kiosk");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("vision-guard");
  network.begin(VISION_GUARD_API_ENDPOINT);
  camera.begin(profile);
  qrScanner.begin();
  workflow.begin();
  visionAi.begin(&network);
  ui.begin();
  eventLog.add("Vision Guard mock kiosk booted");
}

void loop() {
  CameraStatus status = camera.status();
  ui.renderCameraStatus(status);

  String qr;
  if (qrScanner.poll(qr)) {
    eventLog.add("QR scanned: " + qr);
    ui.renderQr(qr);

    InspectionResult result = workflow.run(qr);
    String aiNote = visionAi.classify(qr, status);
    ui.renderChecklist(result);
    ui.renderResult(result, aiNote);

    storage.incrementEventCount();
    network.postEvent(String("{\"source\":\"vision-guard\",\"qr\":\"") + qr + "\",\"result\":\"" + result.status + "\"}");
    Logger::diag("vision_guard_tick", "ok", "events=" + String(storage.eventCount()));
  }

  delay(20);
}
