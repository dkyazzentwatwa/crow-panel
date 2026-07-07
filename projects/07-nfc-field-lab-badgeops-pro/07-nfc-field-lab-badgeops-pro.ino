#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

void refreshNfc(const String &banner) {
  dashboard.setTile(0, "Scan", "ready", "PN532 mock");
  dashboard.setTile(1, "Badge", "UID", "demo policy only");
  dashboard.setTile(2, "NDEF", "Type 4", "public records");
  dashboard.setTile(3, "APDU", "safe", "NDEF AID only");
  dashboard.setTile(4, "Files", "4", "mock SD artifacts");
  dashboard.setTile(5, "Badges", "3", "demo registry");
  dashboard.setBanner(banner);
  dashboard.setFooter("UID-only is demo-grade only; no payment or proprietary APDU probing");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "nfc-field-lab", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdScan(const String &) {
  Serial.println(F("[nfc] uid=04:A1:22:9C type=NTAG213 capacity=144B"));
  eventLog.add("Mock NFC scan");
  dashboard.setDetail("Scan Result", "UID 04:A1:22:9C|Type NTAG213|Capacity 144 bytes");
  refreshNfc("mock tag identified");
}

void cmdTap(const String &args) {
  String uid = args;
  uid.trim();
  if (uid.length() == 0) uid = "04:A1:22:9C";
  String decision = uid == "C2:44:10:AA" ? "DENIED suspended" : "GRANTED demo tech";
  Serial.println(String("[badge] uid=") + uid + " " + decision);
  eventLog.add(String("Badge tap ") + uid + " " + decision);
  dashboard.setDetail("Badge Decision", String("UID ") + uid + "|" + decision + "|Demo policy only");
  refreshNfc("badge decision logged");
}

void cmdNdef(const String &) {
  Serial.println(F("[ndef] url=https://techtiff.ai/lab"));
  dashboard.setDetail("NDEF Preview", "Type 4 NDEF mock|URL https://techtiff.ai/lab|iPhone-friendly handoff path");
  refreshNfc("NDEF record preview");
}

void cmdApdu(const String &) {
  Serial.println(F("[apdu] SELECT NDEF AID -> 90 00; READ NLEN -> 00 1E"));
  dashboard.setDetail("APDU Lab", "SELECT public NDEF AID: 90 00|READ NLEN: 00 1E|No payment AIDs, no proprietary probing");
  refreshNfc("safe APDU lab result");
}

void cmdFiles(const String &) {
  Serial.println(F("[files] scanlog.csv dmp001.json ndef_url.txt audit.txt"));
  dashboard.setDetail("Artifacts", "scanlog.csv|dmp001.json|ndef_url.txt|audit.txt");
  refreshNfc("mock SD artifacts listed");
}

void cmdBadges(const String &) {
  Serial.println(F("[badges] 04:A1:22:9C active; C2:44:10:AA suspended; 11:22:33:44 unknown"));
  dashboard.setDetail("Badge Registry", "04:A1:22:9C active tech|C2:44:10:AA suspended contractor|Unknown badges denied");
  refreshNfc("badge registry shown");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel NFC Field Lab / BadgeOps Pro");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("nfc-field-lab");
  dashboard.begin("NFC FIELD LAB", "BADGEOPS PRO", "PN532 MOCK");
  refreshNfc("ready for safe NFC lab workflow");
  eventLog.add("NFC Field Lab booted");
  router.begin(Serial, "nfc-lab");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "mock PN532 scan", cmdScan);
  router.on("tap", "tap [uid] - mock badge policy", cmdTap);
  router.on("ndef", "preview safe NDEF record", cmdNdef);
  router.on("apdu", "safe Type 4 NDEF APDU probe", cmdApdu);
  router.on("files", "list mock SD artifacts", cmdFiles);
  router.on("badges", "print demo badge registry", cmdBadges);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
