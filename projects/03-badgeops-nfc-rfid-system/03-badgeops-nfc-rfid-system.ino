#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockBadgeReader.h"
#include "src/Pn532Reader.h"
#include "src/Mfrc522Reader.h"
#include "src/BadgeRegistry.h"
#include "src/AccessPolicy.h"
#include "src/BadgeOpsUi.h"

// Reader selection - compile-verified, not hardware-verified. Enable ONE
// real reader at a time (see docs/hardware-bringup-checklist.md, Stage 6).
#if USE_PN532_DRIVER
Pn532Reader badgeReader;
#elif USE_MFRC522_DRIVER
Mfrc522Reader badgeReader;
#else
MockBadgeReader badgeReader;
#endif
BadgeRegistry registry;
AccessPolicy policy;
BadgeOpsUi ui;
EventLog eventLog;
StorageManager storage;
CrowNetworkClient network;
SerialCommandRouter router;

// One pipeline for every tap source: the mock reader, the serial `tap`
// command, and (once hardware-verified) the real PN532/MFRC522 drivers
// all feed this same function.
void processTap(const BadgeRead &read) {
  BadgeRecord record;
  bool found = registry.findByUid(read.uid, record);
  AccessDecision decision = policy.evaluate(read, record, found);

  ui.renderTap(read);
  ui.renderDecision(decision, record, found);
  eventLog.add(decision.message);

  storage.incrementEventCount();
  // Mock JSON, unescaped - swap for real serialization (ArduinoJson)
  // before a backend ingests this for real.
  network.postEvent(String("{\"source\":\"badgeops\",\"uid\":\"") + read.uid + "\",\"decision\":\"" + decision.status + "\"}");
  Logger::diag("badgeops_tick", "ok", "events=" + String(storage.eventCount()));
}

void cmdTap(const String &args) {
  BadgeRead read;
  read.uid = args.length() > 0 ? args : MockData::badgeUid(0);
  read.uid.toUpperCase();  // registry UIDs are uppercase; accept either from the keyboard
  read.reader = "serial";
  read.readAtMs = millis();
  Logger::info("cmd", "injecting tap uid=" + read.uid);
  processTap(read);
}

void cmdBadges(const String &) {
  registry.printAll(Serial);
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "badgeops", storage.eventCount());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel BadgeOps NFC/RFID System");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("badgeops");
  network.begin(BADGEOPS_API_ENDPOINT, WIFI_SSID, WIFI_PASS);
  badgeReader.begin(profile);
  registry.begin();
  policy.begin("lab");
  ui.begin();
  eventLog.add("BadgeOps mock terminal booted");

  router.begin(Serial, "badgeops");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events, oldest first", cmdHistory);
  router.on("badges", "list the badge registry", cmdBadges);
  router.on("tap", "tap [uid] - simulate a badge tap, e.g. tap C2:44:10:AA", cmdTap);
}

void loop() {
  router.poll();
  network.maintain();
  ui.tick();

  BadgeRead read;
  if (badgeReader.poll(read)) {
    processTap(read);
  }

  // Small yield only; demo cadence comes from Throttle gates. Worst-case
  // serial command latency is one pass (~20 ms) - imperceptible.
  delay(20);
}
