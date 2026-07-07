#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockCameraManager.h"
#include "src/QrScanner.h"
#include "src/InspectionWorkflow.h"
#include "src/VisionAiClient.h"
#include "src/VisionGuardUi.h"

MockCameraManager camera;
QrScanner qrScanner;
InspectionWorkflow workflow;
VisionAiClient visionAi;
VisionGuardUi ui;
EventLog eventLog;
StorageManager storage;
CrowNetworkClient network;
SerialCommandRouter router;

// One pipeline for every scan source: the mock scanner, the serial `scan`
// command, and (later) a real camera QR decoder all feed this function.
void processScan(const String &qr) {
  eventLog.add("QR scanned: " + qr);
  ui.renderQr(qr);

  CameraStatus status = camera.status();
  InspectionResult result = workflow.run(qr);
  String aiNote = visionAi.classify(qr, status);
  ui.renderChecklist(result);
  ui.renderResult(result, aiNote);

  storage.incrementEventCount();
  // Mock JSON, unescaped - swap for real serialization (ArduinoJson)
  // before a backend ingests this for real.
  network.postEvent(String("{\"source\":\"vision-guard\",\"qr\":\"") + qr + "\",\"result\":\"" + result.status + "\"}");
  Logger::diag("vision_guard_tick", "ok", "events=" + String(storage.eventCount()));
}

void cmdScan(const String &args) {
  String qr = args.length() > 0 ? args : String("INSPECT-9999");
  Logger::info("cmd", "injecting scan " + qr);
  processScan(qr);
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "vision-guard", storage.eventCount());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Vision Guard Inspection Kiosk");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("vision-guard");
  network.begin(VISION_GUARD_API_ENDPOINT, WIFI_SSID, WIFI_PASS);
  camera.begin(profile);
  qrScanner.begin();
  workflow.begin();
  visionAi.begin(&network);
  ui.begin();
  eventLog.add("Vision Guard mock kiosk booted");

  router.begin(Serial, "vision-guard");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events, oldest first", cmdHistory);
  router.on("scan", "scan [text] - simulate a QR scan, e.g. scan INSPECT-CUSTOM-1", cmdScan);
}

void loop() {
  router.poll();
  network.maintain();
  ui.tick();

  CameraStatus status = camera.status();
  ui.renderCameraStatus(status);

  String qr;
  if (qrScanner.poll(qr)) {
    processScan(qr);
  }

  // Small yield only; demo cadence comes from Throttle gates. Worst-case
  // serial command latency is one pass (~20 ms) - imperceptible.
  delay(20);
}
