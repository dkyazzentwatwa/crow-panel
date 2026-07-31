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
static const uint8_t kActiveReader = 1;
#elif USE_MFRC522_DRIVER
Mfrc522Reader badgeReader;
static const uint8_t kActiveReader = 2;
#else
MockBadgeReader badgeReader;
static const uint8_t kActiveReader = 0;
#endif

static const char *kZone = "lab";

BadgeRegistry registry;
AccessPolicy policy;
BadgeOpsUi ui;
EventLog eventLog;
StorageManager storage;
CrowNetworkClient network;
SerialCommandRouter router;

// One pipeline for every tap source: the mock reader, the serial `tap`
// command, the on-screen quick-tap buttons, and (once hardware-verified) the
// real PN532/MFRC522 drivers all feed this same function. `userInitiated` is
// true for taps a person asked for (serial/touch); those always pop the RESULT
// screen, while the automatic mock cadence only does so on the idle kiosk view.
AccessDecision processTap(const BadgeRead &read, bool userInitiated) {
  BadgeRecord record;
  bool found = registry.findByUid(read.uid, record);
  AccessDecision decision = policy.evaluate(read, record, found);

  ui.renderTap(read);
  ui.renderDecision(decision, record, found, userInitiated);
  eventLog.add(decision.message);

  storage.incrementEventCount();
  // Mock JSON, unescaped - swap for real serialization (ArduinoJson)
  // before a backend ingests this for real.
  network.postEvent(String("{\"source\":\"badgeops\",\"uid\":\"") + read.uid +
                    "\",\"decision\":\"" + decision.status + "\"}");
  Logger::diag("badgeops_tick", "ok", "events=" + String(storage.eventCount()));
  return decision;
}

// Build a BadgeRead from a UID string typed at Serial or tapped on screen.
BadgeRead makeRead(const String &uid, const char *source) {
  BadgeRead read;
  read.uid = uid.length() > 0 ? uid : MockData::badgeUid(0);
  read.uid.toUpperCase();  // registry UIDs are uppercase; accept either from the keyboard
  read.reader = source;
  read.readAtMs = millis();
  return read;
}

void cmdTap(const String &args) {
  BadgeRead read = makeRead(args, "serial");
  Logger::info("cmd", "injecting tap uid=" + read.uid);
  processTap(read, /*userInitiated=*/true);
}

void cmdBadges(const String &) {
  registry.printAll(Serial);
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "badgeops", storage.eventCount(), &router);
  Serial.print(F("[badgeops] reader="));
  Serial.print(badgeReader.driverName());
  Serial.print(F(" ready="));
  Serial.print(badgeReader.ready() ? F("yes") : F("no"));
  Serial.print(F(" screen="));
  Serial.print(ui.screenName());
  Serial.print(F(" decisions="));
  Serial.println(ui.attendanceCount());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdTouch(const String &) {
  ui.printTouch(Serial);
}

void cmdScreen(const String &args) {
  if (args.length() == 0) {
    Serial.print(F("[screen] current="));
    Serial.println(ui.screenName());
    Serial.println(F("[screen] usage: screen <tap|result|registry|attendance|readers>"));
    return;
  }
  if (ui.showScreenByName(args)) {
    Serial.print(F("[screen] -> "));
    Serial.println(ui.screenName());
  } else {
    Serial.println(F("[screen] usage: screen <tap|result|registry|attendance|readers>"));
  }
}

void cmdReader(const String &args) {
  if (args.length() == 0) {
    Serial.print(F("[reader] active="));
    Serial.print(badgeReader.driverName());
    Serial.print(F(" ready="));
    Serial.println(badgeReader.ready() ? F("yes") : F("no"));
    Serial.println(F("[reader] the live reader is chosen at compile time (-DUSE_*_DRIVER)."));
    Serial.println(F("[reader] usage: reader <mock|pn532|mfrc522> - inspect that reader's card"));
    return;
  }
  if (ui.selectReaderByName(args)) {
    Serial.print(F("[reader] inspecting "));
    Serial.println(args);
  } else {
    Serial.println(F("[reader] usage: reader <mock|pn532|mfrc522>"));
  }
}

// End-to-end mock check: exercises the registry, the access policy, and the UI
// plumbing (attendance ring + screen transitions) with no panel attached, and
// prints an explicit PASS/FAIL per step plus a summary line.
bool runSelfTest() {
  Serial.println(F("[selftest] BadgeOps mock flow (zone lab)"));
  int pass = 0, total = 0;
  auto check = [&](const char *name, bool ok) {
    total++;
    if (ok) pass++;
    Serial.print(ok ? F("[selftest] PASS ") : F("[selftest] FAIL "));
    Serial.println(name);
  };

  struct PolicyCase {
    const char *uid;
    const char *expect;  // "granted" | "denied"
    const char *label;
  };
  static const PolicyCase cases[] = {
    {"04:A1:22:9C", "granted", "active technician -> granted"},
    {"19:8C:52:F1", "granted", "active admin -> granted"},
    {"7A:31:90:0D", "denied", "wrong zone -> denied"},
    {"C2:44:10:AA", "denied", "suspended -> denied"},
    {"88:90:A1:07", "denied", "expired -> denied"},
    {"11:22:33:44", "denied", "unknown uid -> denied"},
  };
  for (const PolicyCase &c : cases) {
    BadgeRead read = makeRead(c.uid, "selftest");
    BadgeRecord record;
    bool found = registry.findByUid(read.uid, record);
    AccessDecision d = policy.evaluate(read, record, found);
    check(c.label, d.status == c.expect);
  }

  // Registry accessors used by the on-panel list.
  check("registry non-empty", registry.count() > 0);
  {
    BadgeRecord rec;
    check("known uid resolves", registry.findByUid("04:A1:22:9C", rec) && rec.name.length() > 0);
  }
  {
    BadgeRecord rec;
    check("unknown uid rejected", !registry.findByUid("11:22:33:44", rec));
  }

  // UI plumbing: a user-initiated decision records to attendance and pops RESULT.
  // Assert on the newest logged UID rather than the count, which saturates once
  // the 16-entry ring is full during a long-running demo.
  ui.showScreen(SCR_TAP);
  {
    BadgeRead read = makeRead("04:A1:22:9C", "selftest");
    BadgeRecord record;
    bool found = registry.findByUid(read.uid, record);
    AccessDecision d = policy.evaluate(read, record, found);
    ui.renderTap(read);
    ui.renderDecision(d, record, found, /*present=*/true);
  }
  check("decision recorded in attendance", String(ui.lastDecisionUid()) == "04:A1:22:9C");
  check("result screen presented", ui.screen() == SCR_RESULT);

  // Navigation parity with the tab bar.
  ui.showScreen(SCR_REGISTRY);
  check("navigate to registry", String(ui.screenName()) == "registry");
  ui.showScreen(SCR_READERS);
  check("navigate to readers", String(ui.screenName()) == "readers");
  check("reader select by name", ui.selectReaderByName("pn532"));
  ui.showScreen(SCR_TAP);
  check("navigate back to tap", String(ui.screenName()) == "tap");

  Serial.print(F("[selftest] summary "));
  Serial.print(pass);
  Serial.print('/');
  Serial.print(total);
  Serial.println(pass == total ? F(" PASS") : F(" FAIL"));
  return pass == total;
}

void cmdSelfTest(const String &) {
  bool ok = runSelfTest();
  eventLog.add(ok ? "selftest PASS" : "selftest FAIL");
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
  policy.begin(kZone);
  ui.begin(&registry, kZone);
  ui.setReaderInfo(badgeReader.driverName(), kActiveReader, badgeReader.ready());
  eventLog.add("BadgeOps terminal booted");

  router.begin(Serial, "badgeops");
  router.on("status", "uptime, heap, profile, flags, reader, screen", cmdStatus);
  router.on("history", "recent events, oldest first", cmdHistory);
  router.on("badges", "list the badge registry", cmdBadges);
  router.on("tap", "tap [uid] - simulate a badge tap, e.g. tap C2:44:10:AA", cmdTap);
  router.on("screen", "screen <tap|result|registry|attendance|readers>", cmdScreen);
  router.on("reader", "reader <mock|pn532|mfrc522> - inspect a reader card", cmdReader);
  router.on("touch", "print raw + mapped touch coords, tap count, screen", cmdTouch);
  router.on("selftest", "drive the mock flow headlessly with PASS/FAIL lines", cmdSelfTest);
}

void loop() {
  router.poll();
  network.maintain();

  // Touch-driven actions come back as events; the UI never runs the pipeline
  // itself. A quick-tap button or badge-detail "simulate" returns EV_TAP_UID.
  BadgeOpsEvent ev = ui.tick();
  if (ev.type == EV_TAP_UID) {
    BadgeRead read = makeRead(ev.arg, "touch");
    processTap(read, ev.userInitiated);
  }

  // Hardware / mock reader cadence. Mock taps are not user-initiated, so they
  // do not interrupt the operator while browsing other screens.
  BadgeRead read;
  if (badgeReader.poll(read)) {
    processTap(read, /*userInitiated=*/false);
  }

  // Small yield only; demo cadence comes from Throttle gates. Worst-case
  // serial command latency is one pass (~20 ms) - imperceptible.
  delay(20);
}
