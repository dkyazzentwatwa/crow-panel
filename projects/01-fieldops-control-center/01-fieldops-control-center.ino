#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockLoRaGateway.h"
#include "src/AlertEngine.h"
#include "src/AiSummaryClient.h"
#include "src/FieldOpsUi.h"
#include "src/EventLog.h"

MockLoRaGateway gateway;
AlertEngine alerts;
AiSummaryClient summaries;
FieldOpsUi ui;
EventLog eventLog;
StorageManager storage;
NetworkClient network;

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel FieldOps Control Center");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("fieldops");
  network.begin(FIELDOPS_API_ENDPOINT);
  gateway.begin(profile);
  summaries.begin(&network);
  ui.begin();
  eventLog.add("FieldOps mock dashboard booted");
}

void loop() {
  SensorPacket packet;
  if (gateway.poll(packet)) {
    ui.renderDashboard(packet);

    String alert = alerts.evaluate(packet);
    if (alert.length() > 0) {
      eventLog.add(alert);
      ui.renderAlert(alert);
    }

    storage.incrementEventCount();
    network.postEvent(String("{\"source\":\"fieldops\",\"node\":\"") + packet.nodeId + "\"}");

    String summary = summaries.summarize(packet, alert);
    ui.renderSummary(summary);

    Logger::diag("fieldops_tick", "ok", "events=" + String(storage.eventCount()));
  }

  delay(20);
}
