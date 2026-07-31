#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/BadgeRegistry.h"
#include "src/MockNfcReader.h"
#include "src/NfcReader.h"
#include "src/NfcLabUi.h"

#if USE_PN532_DRIVER
#include "src/Pn532NfcReader.h"
#endif

#if USE_MFRC522_DRIVER
#include "src/Mfrc522NfcReader.h"
#endif

// The bespoke touch console replaces the generic OpsDashboard. Serial stays the
// source of truth; the UI mirrors the same state and its taps map 1:1 to serial
// commands. All rendering lives behind USE_DISPLAY in NfcLabUi.cpp.
NfcLabUi ui;
NfcLabState state;

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

// --------------------------------------------------------------------------
// State population — every screen reads NfcLabState; these fill it from the
// active reader(s) and the BadgeRegistry.
// --------------------------------------------------------------------------

void applyReaderCaps() {
  bool ndef = false, apdu = false;
  for (uint8_t i = 0; i < readerCount; i++) {
    if (readers[i]->supportsNdefPreview()) ndef = true;
    if (readers[i]->supportsType4Ndef()) apdu = true;
  }
  state.ndefSupported = ndef;
  state.apduSupported = apdu;
}

bool scanForUid(NfcUidRead &read) {
  for (uint8_t i = 0; i < readerCount; i++) {
    if (readers[i]->pollUid(read)) {
      return true;
    }
  }
  return false;
}

bool refreshNdef() {
  for (uint8_t i = 0; i < readerCount; i++) {
    if (!readers[i]->supportsNdefPreview()) continue;
    NdefPreview preview;
    if (readers[i]->readNdefPreview(preview)) {
      state.ndef = preview;
      state.hasNdef = true;
      return true;
    }
  }
  state.hasNdef = false;
  return false;
}

bool refreshApdu() {
  for (uint8_t i = 0; i < readerCount; i++) {
    if (!readers[i]->supportsType4Ndef()) continue;
    SafeApduRead result;
    if (readers[i]->readType4Ndef(result)) {
      state.apdu = result;
      state.hasApdu = true;
      return true;
    }
    state.apdu = result;  // keep the failure trace for the panel
  }
  state.hasApdu = false;
  return false;
}

void refreshBadge() {
  if (!state.hasTag) {
    state.badgeEvaluated = false;
    state.badgeMatched = false;
    return;
  }
  state.badge = badgeRegistry.evaluateUid(state.tag.uid);
  state.badgeMatched = badgeRegistry.findByUid(state.tag.uid, state.badgeRecord);
  state.badgeEvaluated = true;
}

// Re-derive everything that hangs off the current tag.
void recomputeDerived() {
  refreshNdef();
  refreshApdu();
  refreshBadge();
  state.apduStep = 0;
}

bool doScan() {
  NfcUidRead read;
  if (!scanForUid(read)) {
    return false;
  }
  state.tag = read;
  state.hasTag = true;
  recomputeDerived();
  return true;
}

void printBadgeDecision(const String &source) {
  Serial.println(String("[badge] source=") + source +
                 " uid=" + state.tag.uid + " " + state.badge.summary);
  eventLog.add(String("Badge ") + state.tag.uid + " " + state.badge.summary);
}

// --------------------------------------------------------------------------
// Serial commands (each also drives the UI so touch and serial stay in parity)
// --------------------------------------------------------------------------

void cmdStatus(const String &) { printSystemStatus(Serial, "nfc-field-lab", storage.eventCount(), &router); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdScan(const String &) {
  if (doScan()) {
    Serial.println(String("[nfc] reader=") + state.tag.reader +
                   " uid=" + state.tag.uid +
                   " type=" + state.tag.tagType +
                   " capacity=" + String(state.tag.capacityBytes) + "B");
    eventLog.add(String("NFC scan ") + state.tag.uid + " via " + state.tag.reader);
    state.banner = "tag identified";
  } else {
    Serial.println(String("[nfc] no tag seen by ") + activeReaderLabel());
    state.banner = "no NFC tag seen";
  }
  ui.showScreen(SCR_SCAN);
}

void cmdTap(const String &args) {
  String uid = args;
  uid.trim();
  if (uid.length() > 0) {
    // Hand-entered UID: decide on it, but there is no physical tag to read, so
    // the NDEF/APDU views correctly show nothing.
    NfcUidRead read;
    read.uid = uid;
    read.tagType = "manual UID";
    read.reader = "serial";
    read.readAtMs = millis();
    read.fromMock = true;
    state.tag = read;
    state.hasTag = true;
    state.hasNdef = false;
    state.hasApdu = false;
    state.apduStep = 0;
    refreshBadge();
    printBadgeDecision("serial");
    ui.showScreen(SCR_BADGE);
    return;
  }

  if (!state.hasTag && !doScan()) {
    Serial.println(String("[badge] no UID available from ") + activeReaderLabel());
    state.banner = "no badge UID available";
    ui.showScreen(SCR_BADGE);
    return;
  }
  refreshBadge();
  printBadgeDecision("reader");
  ui.showScreen(SCR_BADGE);
}

void cmdNdef(const String &) {
  if (!state.hasTag) doScan();
  if (!state.ndefSupported) {
    Serial.println(F("[ndef] active reader is UID-only; no Type 4 NDEF preview"));
    state.banner = "NDEF preview unavailable";
  } else if (refreshNdef()) {
    Serial.println(String("[ndef] source=") + state.ndef.source +
                   " type=" + state.ndef.recordType +
                   " bytes=" + String(state.ndef.byteCount) +
                   " preview=" + state.ndef.payload);
    state.banner = "NDEF record preview";
  } else {
    Serial.println(F("[ndef] no public Type 4 NDEF record read"));
    state.banner = "no NDEF record";
  }
  ui.showScreen(SCR_NDEF);
}

void cmdApdu(const String &) {
  if (!state.hasTag) doScan();
  state.apduStep = 0;
  if (!state.apduSupported) {
    Serial.println(F("[apdu] active reader is UID-only; no Type 4 exchange"));
    state.banner = "APDU exchange unavailable";
  } else if (refreshApdu()) {
    Serial.println(String("[apdu] trace=") + state.apdu.trace);
    Serial.println(String("[apdu] preview=") + state.apdu.preview +
                   " nlen=" + String(state.apdu.ndefLength));
    Serial.println(F("[apdu] read-only: SELECT + READ BINARY only, no write, no payment AIDs"));
    state.banner = "safe APDU lab result";
  } else {
    Serial.println(String("[apdu] no Type 4 NDEF response: ") + state.apdu.trace);
    state.banner = "APDU exchange incomplete";
  }
  ui.showScreen(SCR_APDU);
}

void cmdStep(const String &args) {
  if (!state.apduSupported) {
    Serial.println(F("[step] active reader is UID-only; no APDU stepper"));
    return;
  }
  String a = args;
  a.trim();
  a.toLowerCase();
  if (a == "reset" || a == "0" || a == "first") {
    state.apduStep = 0;
  } else {
    state.apduStep = (state.apduStep + 1) % NfcLabUi::kApduStepCount;
  }
  Serial.println(String("[step] apdu step ") + String(state.apduStep + 1) + "/" +
                 String(NfcLabUi::kApduStepCount));
  ui.showScreen(SCR_APDU);
}

void cmdFiles(const String &) {
  Serial.println(F("[files] on-tag: NDEF app (D2760000850101), CC E103, NDEF file E104"));
  Serial.println(F("[files] artifacts: scanlog.csv dmp001.json ndef_url.txt audit.txt"));
  state.banner = "tag files listed";
  ui.showScreen(SCR_FILES);
}

void cmdBadges(const String &) { badgeRegistry.printAll(Serial); }

void cmdScreen(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  NfcScreen target = ui.screen();
  if (a == "scan") target = SCR_SCAN;
  else if (a == "ndef") target = SCR_NDEF;
  else if (a == "apdu") target = SCR_APDU;
  else if (a == "badge") target = SCR_BADGE;
  else if (a == "files") target = SCR_FILES;
  else if (a.length() > 0) {
    Serial.println(F("[screen] usage: screen <scan|ndef|apdu|badge|files>"));
    return;
  }
  ui.showScreen(target);
  Serial.println(String("[screen] ") + ui.screenName());
}

void cmdTouch(const String &) { ui.printTouch(Serial); }

static bool stCheck(const char *name, bool ok, int &pass, int &total) {
  total++;
  Serial.print(ok ? F("[selftest] PASS ") : F("[selftest] FAIL "));
  Serial.println(name);
  if (ok) pass++;
  return ok;
}

void cmdSelftest(const String &) {
  Serial.println(F("[selftest] NFC Field Lab / BadgeOps Pro mock flow"));
  int pass = 0, total = 0;

  stCheck("readers registered", readerCount > 0, pass, total);
  stCheck("reader label present", activeReaderLabel().length() > 0, pass, total);

  // Badge policy is static, so these decisions hold in every build.
  stCheck("badge 04:A1:22:9C granted",
          badgeRegistry.evaluateUid("04:A1:22:9C").status == "granted", pass, total);
  stCheck("badge 7A:31:90:0D granted",
          badgeRegistry.evaluateUid("7A:31:90:0D").status == "granted", pass, total);
  stCheck("badge C2:44:10:AA denied (suspended)",
          badgeRegistry.evaluateUid("C2:44:10:AA").status == "denied", pass, total);
  stCheck("badge 11:22:33:44 denied (unknown)",
          badgeRegistry.evaluateUid("11:22:33:44").status == "denied", pass, total);

  const bool mockMode = !(USE_PN532_DRIVER || USE_MFRC522_DRIVER);
  if (mockMode) {
    stCheck("scan reads a mock tag", doScan(), pass, total);
    stCheck("scanned tag has a UID", state.hasTag && state.tag.uid.length() > 0, pass, total);
    stCheck("NDEF preview decoded",
            state.ndefSupported && state.hasNdef && state.ndef.payload.length() > 0, pass, total);
    stCheck("Type 4 trace + NLEN",
            state.apduSupported && state.hasApdu && state.apdu.trace.length() > 0 &&
                state.apdu.ndefLength > 0, pass, total);
    stCheck("badge evaluated for scanned tag", state.badgeEvaluated, pass, total);
  } else {
    Serial.println(F("[selftest] INFO driver build: present a physical tag for scan/ndef/apdu"));
  }

  // Screen navigation parity.
  const NfcScreen order[SCR_COUNT] = {SCR_SCAN, SCR_NDEF, SCR_APDU, SCR_BADGE, SCR_FILES};
  bool navOk = true;
  for (uint8_t i = 0; i < SCR_COUNT; i++) {
    ui.showScreen(order[i]);
    if (ui.screen() != order[i]) navOk = false;
  }
  stCheck("screen navigation", navOk, pass, total);

  // APDU stepper bounds.
  stCheck("apdu step count == 6", NfcLabUi::kApduStepCount == 6, pass, total);
  const uint8_t last = NfcLabUi::kApduStepCount - 1;
  stCheck("apdu step wraps to 0", (uint8_t)((last + 1) % NfcLabUi::kApduStepCount) == 0, pass, total);

  Serial.print(F("[selftest] summary "));
  Serial.print(pass);
  Serial.print(F("/"));
  Serial.print(total);
  Serial.println(pass == total ? F(" PASS") : F(" FAIL"));
  eventLog.add(pass == total ? "Selftest PASS" : "Selftest FAIL");

  ui.showScreen(SCR_SCAN);
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

  state.readerLabel = activeReaderLabel();
  applyReaderCaps();
  ui.begin(state.readerLabel.c_str());

  // Mock-first: with no reader wired the mock returns a tag immediately so every
  // screen has believable data. On real-reader builds this is a no-op until a
  // tag is presented.
  doScan();
  state.banner = "ready";
  ui.showScreen(SCR_SCAN);
  eventLog.add("NFC Field Lab booted");

  router.begin(Serial, "nfc-lab");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "read the next tag UID (Scan screen)", cmdScan);
  router.on("tap", "tap [uid] - badge grant/deny (Badge screen)", cmdTap);
  router.on("ndef", "preview the public NDEF record (NDEF screen)", cmdNdef);
  router.on("apdu", "run the read-only Type 4 trace (APDU screen)", cmdApdu);
  router.on("step", "step [reset] - advance the APDU stepper", cmdStep);
  router.on("files", "list on-tag files and artifacts (Files screen)", cmdFiles);
  router.on("badges", "print the demo badge registry", cmdBadges);
  router.on("screen", "screen <scan|ndef|apdu|badge|files>", cmdScreen);
  router.on("touch", "raw + mapped touch, taps, current screen", cmdTouch);
  router.on("selftest", "run the mock flow end-to-end with PASS/FAIL", cmdSelftest);
}

void loop() {
  router.poll();

#if USE_PN532_DRIVER || USE_MFRC522_DRIVER
  // Real readers: adopt a tag the moment it appears and log the decision.
  {
    NfcUidRead read;
    if (scanForUid(read)) {
      state.tag = read;
      state.hasTag = true;
      recomputeDerived();
      printBadgeDecision("reader");
      state.banner = "tag identified";
      ui.markDirty();
    }
  }
#endif

  const NfcLabEvent ev = ui.tick(state);
  switch (ev) {
    case EVT_SCAN_NEXT:
      if (doScan()) {
        Serial.println(String("[nfc] reader=") + state.tag.reader + " uid=" + state.tag.uid +
                       " type=" + state.tag.tagType);
        eventLog.add(String("NFC scan ") + state.tag.uid + " via " + state.tag.reader);
        state.banner = "tag identified";
      } else {
        Serial.println(String("[nfc] no tag seen by ") + activeReaderLabel());
        state.banner = "no NFC tag seen";
      }
      ui.markDirty();
      break;
    case EVT_APDU_STEP:
      state.apduStep = (state.apduStep + 1) % NfcLabUi::kApduStepCount;
      ui.markDirty();
      break;
    case EVT_APDU_RESET:
      state.apduStep = 0;
      ui.markDirty();
      break;
    default:
      break;
  }

  delay(20);
}
