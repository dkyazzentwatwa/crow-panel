#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockBadgeReader.h"
#include "src/BadgeRegistry.h"
#include "src/AccessPolicy.h"
#include "src/BadgeOpsUi.h"
#include "src/EventLog.h"

MockBadgeReader badgeReader;
BadgeRegistry registry;
AccessPolicy policy;
BadgeOpsUi ui;
EventLog eventLog;
StorageManager storage;
NetworkClient network;

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel BadgeOps NFC/RFID System");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("badgeops");
  network.begin(BADGEOPS_API_ENDPOINT);
  badgeReader.begin(profile);
  registry.begin();
  policy.begin("lab");
  ui.begin();
  eventLog.add("BadgeOps mock terminal booted");
}

void loop() {
  BadgeRead read;
  if (badgeReader.poll(read)) {
    BadgeRecord record;
    bool found = registry.findByUid(read.uid, record);
    AccessDecision decision = policy.evaluate(read, record, found);

    ui.renderTap(read);
    ui.renderDecision(decision, record, found);
    eventLog.add(decision.message);

    storage.incrementEventCount();
    network.postEvent(String("{\"source\":\"badgeops\",\"uid\":\"") + read.uid + "\",\"decision\":\"" + decision.status + "\"}");
    Logger::diag("badgeops_tick", "ok", "events=" + String(storage.eventCount()));
  }

  delay(20);
}
