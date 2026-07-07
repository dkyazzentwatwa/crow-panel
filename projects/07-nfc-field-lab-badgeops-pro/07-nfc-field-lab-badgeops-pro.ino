#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/BadgeRegistry.h"
#include "src/MockNfcReader.h"
#include "src/NfcReader.h"

#if USE_PN532_DRIVER
#include "src/Pn532NfcReader.h"
#endif

#if USE_MFRC522_DRIVER
#include "src/Mfrc522NfcReader.h"
#endif

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
BadgeRegistry badgeRegistry;

NfcReader *readers[3];
uint8_t readerCount = 0;

#if USE_PN532_DRIVER
Pn532NfcReader pn532Reader;
#endif

#if USE_MFRC522_DRIVER
Mfrc522NfcReader mfrc522Reader;
#endif

#if !USE_PN532_DRIVER && !USE_MFRC522_DRIVER
MockNfcReader mockReader;
#endif

void addReader(NfcReader &reader) {
  if (readerCount < sizeof(readers) / sizeof(readers[0])) {
    readers[readerCount++] = &reader;
  }
}

void registerReaders() {
  readerCount = 0;
#if USE_PN532_DRIVER
  addReader(pn532Reader);
#endif
#if USE_MFRC522_DRIVER
  addReader(mfrc522Reader);
#endif
#if !USE_PN532_DRIVER && !USE_MFRC522_DRIVER
  addReader(mockReader);
#endif
}

String activeReaderLabel() {
  String label;
  for (uint8_t i = 0; i < readerCount; i++) {
    if (i > 0) {
      label += "+";
    }
    label += readers[i]->driverName();
  }
  return label.length() > 0 ? label : "none";
}

void refreshNfc(const String &banner) {
  dashboard.setTile(0, "Scan", "ready", activeReaderLabel());
  dashboard.setTile(1, "Badge", "UID", "demo policy only");
  dashboard.setTile(2, "NDEF", "Type 4", "public records");
  dashboard.setTile(3, "APDU", "safe", "NDEF AID only");
  dashboard.setTile(4, "Files", "4", "mock SD artifacts");
  dashboard.setTile(5, "Badges", String(badgeRegistry.count()), "demo registry");
  dashboard.setBanner(banner);
  dashboard.setFooter("UID-only is demo-grade only; no payment or proprietary APDU probing");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "nfc-field-lab", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

bool scanForUid(NfcUidRead &read) {
  for (uint8_t i = 0; i < readerCount; i++) {
    if (readers[i]->pollUid(read)) {
      return true;
    }
  }
  return false;
}

void showBadgeDecision(const NfcUidRead &read, const String &source) {
  const BadgeDecision decision = badgeRegistry.evaluateUid(read.uid);
  Serial.println(String("[badge] source=") + source +
                 " reader=" + read.reader +
                 " uid=" + read.uid +
                 " " + decision.summary);
  eventLog.add(String("Badge ") + read.uid + " " + decision.summary);
  dashboard.setDetail("Badge Decision",
                      String("UID ") + read.uid + "|" +
                      decision.summary + "|" +
                      decision.detail + "|Demo policy only");
  refreshNfc("badge decision logged");
}

void cmdScan(const String &) {
  NfcUidRead read;
  if (!scanForUid(read)) {
    Serial.println(String("[nfc] no tag seen by ") + activeReaderLabel());
    dashboard.setDetail("Scan Result", "No tag seen|Use mock mode or present a UID tag to an enabled reader");
    refreshNfc("no NFC tag seen");
    return;
  }

  Serial.println(String("[nfc] reader=") + read.reader +
                 " uid=" + read.uid +
                 " type=" + read.tagType +
                 " capacity=" + String(read.capacityBytes) + "B");
  eventLog.add(String("NFC scan ") + read.uid + " via " + read.reader);
  dashboard.setDetail("Scan Result",
                      String("UID ") + read.uid + "|" +
                      read.tagType + "|Reader " + read.reader);
  refreshNfc("tag identified");
}

void cmdTap(const String &args) {
  String uid = args;
  uid.trim();
  NfcUidRead read;
  if (uid.length() > 0) {
    read.uid = uid;
    read.tagType = "manual UID";
    read.reader = "serial";
    read.readAtMs = millis();
    read.fromMock = true;
    showBadgeDecision(read, "serial");
    return;
  }

  if (!scanForUid(read)) {
    Serial.println(String("[badge] no UID available from ") + activeReaderLabel());
    dashboard.setDetail("Badge Decision", "No UID available|Use tap [uid] or present a tag");
    refreshNfc("no badge UID available");
    return;
  }
  showBadgeDecision(read, "reader");
}

void cmdNdef(const String &) {
  bool capableReader = false;
  for (uint8_t i = 0; i < readerCount; i++) {
    if (!readers[i]->supportsNdefPreview()) {
      continue;
    }
    capableReader = true;
    NdefPreview preview;
    if (readers[i]->readNdefPreview(preview)) {
      Serial.println(String("[ndef] source=") + preview.source +
                     " type=" + preview.recordType +
                     " bytes=" + String(preview.byteCount) +
                     " preview=" + preview.payload);
      dashboard.setDetail("NDEF Preview",
                          preview.recordType + "|" + preview.payload +
                          "|Read-only public NDEF preview");
      refreshNfc("NDEF record preview");
      return;
    }
  }

  Serial.println(capableReader ?
                 F("[ndef] no public Type 4 NDEF record read") :
                 F("[ndef] no active reader supports Type 4 NDEF preview"));
  dashboard.setDetail("NDEF Preview", "No public Type 4 NDEF record read|PN532 or mock mode required");
  refreshNfc("NDEF preview unavailable");
}

void cmdApdu(const String &) {
  bool capableReader = false;
  for (uint8_t i = 0; i < readerCount; i++) {
    if (!readers[i]->supportsType4Ndef()) {
      continue;
    }
    capableReader = true;
    SafeApduRead result;
    if (readers[i]->readType4Ndef(result)) {
      Serial.println(String("[apdu] source=") + result.source +
                     " trace=" + result.trace +
                     " preview=" + result.preview);
      dashboard.setDetail("APDU Lab",
                          result.trace + "|" + result.preview +
                          "|Read-only NDEF APDUs only");
      refreshNfc("safe APDU lab result");
      return;
    }
    Serial.println(String("[apdu] source=") + result.source + " " + result.trace);
  }

  Serial.println(capableReader ?
                 F("[apdu] no public Type 4 NDEF APDU response read") :
                 F("[apdu] no active reader supports Type 4 NDEF APDU reads"));
  dashboard.setDetail("APDU Lab", "No public Type 4 NDEF response|No payment AIDs or proprietary probing");
  refreshNfc("safe APDU lab unavailable");
}

void cmdFiles(const String &) {
  Serial.println(F("[files] scanlog.csv dmp001.json ndef_url.txt audit.txt"));
  dashboard.setDetail("Artifacts", "scanlog.csv|dmp001.json|ndef_url.txt|audit.txt");
  refreshNfc("mock SD artifacts listed");
}

void cmdBadges(const String &) {
  badgeRegistry.printAll(Serial);
  dashboard.setDetail("Badge Registry", "04:A1:22:9C active tech|C2:44:10:AA suspended contractor|Unknown badges denied");
  refreshNfc("badge registry shown");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel NFC Field Lab / BadgeOps Pro");
  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);
  storage.begin("nfc-field-lab");
  badgeRegistry.begin();
  registerReaders();
  for (uint8_t i = 0; i < readerCount; i++) {
    readers[i]->begin(profile);
  }
  dashboard.begin("NFC FIELD LAB", "BADGEOPS PRO", activeReaderLabel().c_str());
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
#if USE_PN532_DRIVER || USE_MFRC522_DRIVER
  NfcUidRead read;
  if (scanForUid(read)) {
    showBadgeDecision(read, "reader");
  }
#endif
  dashboard.tick();
  delay(20);
}
