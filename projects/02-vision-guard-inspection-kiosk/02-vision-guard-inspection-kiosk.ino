#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockCameraManager.h"
#include "src/QrScanner.h"
#include "src/InspectionWorkflow.h"
#include "src/VisionAiClient.h"
#include "src/VisionGuardUi.h"

// Project 02: Vision Guard Inspection Kiosk. A touch-first inspection console
// for the CrowPanel Advanced ESP32-P4. The camera is an honest stub - the
// ESP32-P4 MIPI-CSI path (esp_video) has no esp32-camera Arduino driver - so
// the Live screen renders a placeholder viewfinder, never a fake frame. Every
// screen fills with believable mock data and every touch action maps 1:1 to a
// serial command through SerialCommandRouter.

MockCameraManager camera;
QrScanner qrScanner;
InspectionWorkflow workflow;
VisionAiClient visionAi;
VisionGuardUi ui;
EventLog eventLog;
StorageManager storage;
CrowNetworkClient network;
SerialCommandRouter router;

static uint32_t mockCodeSeq = 2000;

static String nextMockCode() {
  return String("INSPECT-") + String(mockCodeSeq++);
}

// One pipeline for every scan source: the touch CAPTURE buttons, the serial
// `scan` command, and the background mock scanner all feed this. makeCurrent
// selects the run for the Checklist/Result screens; background scans only grow
// the audit log so they never hijack a review in progress.
static const InspectionRun &runInspection(const String &qr, bool makeCurrent) {
  eventLog.add("QR scanned: " + qr);
  CameraStatus status = camera.status();

  const InspectionRun &r = workflow.recordScan(qr, makeCurrent);

  int failIdx = -1;
  for (uint8_t i = 0; i < workflow.itemCount(); i++) {
    if (r.items[i] == ITEM_FAIL) { failIdx = (int)i; break; }
  }
  const bool pass = (failIdx < 0);
  const char *concern = pass ? nullptr : workflow.itemName((uint8_t)failIdx);
  String note = visionAi.classify(qr, status, pass, concern);
  workflow.setNoteForRun(r.id, note);

  storage.incrementEventCount();
  // Mock JSON, unescaped - swap for real serialization (ArduinoJson) before a
  // backend ingests this for real.
  network.postEvent(String("{\"source\":\"vision-guard\",\"qr\":\"") + qr +
                    "\",\"result\":\"" + (pass ? "pass" : "fail") + "\"}");
  ui.markDirty();
  Logger::diag("vision_guard_scan", pass ? "pass" : "fail",
               "qr=" + qr + " events=" + String(storage.eventCount()));
  return r;
}

// Touch CAPTURE + serial `scan`: run a fresh inspection and open the checklist.
static void doScan(const String &code) {
  String qr = code.length() > 0 ? code : nextMockCode();
  runInspection(qr, /*makeCurrent=*/true);
  ui.showScreen(SCR_CHECKS);
}

static ItemState parseState(const String &raw) {
  String s = raw;
  s.trim();
  s.toLowerCase();
  if (s.startsWith("p")) return ITEM_PASS;
  if (s.startsWith("f")) return ITEM_FAIL;
  if (s.startsWith("s")) return ITEM_SKIP;
  return ITEM_PENDING;  // sentinel: invalid
}

// ---------------------------------------------------------------------------
static void cmdStatus(const String &) {
  printSystemStatus(Serial, "vision-guard", storage.eventCount());
  Serial.print(F("[vision] camera source="));
  Serial.print(camera.driverName());
  Serial.print(F(" stub="));
  Serial.print(cameraStubReason());
  Serial.print(F(" ("));
  Serial.print(cameraHardwareNote());
  Serial.println(F(")"));
  Serial.print(F("[vision] runs="));
  Serial.print(workflow.historyCount());
  Serial.print(F(" total_scans="));
  Serial.print(workflow.totalRuns());
  Serial.print(F(" screen="));
  Serial.println(ui.screenName());
  if (workflow.hasCurrent()) {
    const InspectionRun &r = workflow.current();
    Serial.print(F("[vision] current qr="));
    Serial.print(r.qr);
    Serial.print(F(" status="));
    Serial.print(r.failStatus ? F("FAIL") : F("PASS"));
    Serial.print(F(" "));
    Serial.print(r.passed);
    Serial.print(F("/"));
    Serial.print(workflow.itemCount());
    Serial.print(F(" reason="));
    Serial.println(r.reason);
  }
}

static void cmdHistory(const String &) {
  uint8_t n = workflow.historyCount();
  Serial.print(F("[history] "));
  Serial.print(n);
  Serial.println(F(" runs (newest first)"));
  for (uint8_t i = 0; i < n; i++) {
    const InspectionRun &r = workflow.runAt(i);
    Serial.printf("  #%u  %-14s  %s  %u/%u  %s\n", i, r.qr,
                  r.failStatus ? "FAIL" : "PASS", r.passed, workflow.itemCount(), r.reason);
  }
}

static void cmdScan(const String &args) {
  Logger::info("cmd", "scan " + (args.length() ? args : String("<auto>")));
  doScan(args);
  ui.renderSerial(Serial);
}

static void cmdCheck(const String &args) {
  if (!workflow.hasCurrent()) {
    Serial.println(F("[check] no run yet; use `scan` first"));
    return;
  }
  String a = args;
  a.trim();
  if (a.length() == 0) {
    Serial.println(F("usage: check <0-6> [pass|fail|skip]  (omit state to cycle)"));
    return;
  }
  int sp = a.indexOf(' ');
  String idxStr = sp < 0 ? a : a.substring(0, sp);
  int i = idxStr.toInt();
  if (i < 0 || i >= (int)workflow.itemCount()) {
    Serial.println(F("[check] index out of range (0-6)"));
    return;
  }
  if (sp < 0) {
    workflow.cycleItem((uint8_t)i);
  } else {
    ItemState s = parseState(a.substring(sp + 1));
    if (s == ITEM_PENDING) {
      Serial.println(F("[check] state must be pass|fail|skip"));
      return;
    }
    workflow.setItem((uint8_t)i, s);
  }
  ui.showScreen(SCR_CHECKS);
  ui.renderSerial(Serial);
}

static void cmdEval(const String &) {
  if (!workflow.hasCurrent()) {
    Serial.println(F("[eval] no run yet; use `scan` first"));
    return;
  }
  workflow.reEvaluateCurrent();
  ui.showScreen(SCR_CHECKS);
  ui.renderSerial(Serial);
}

static void cmdOpen(const String &args) {
  int n = args.toInt();
  if (args.length() == 0 || n < 0 || n >= (int)workflow.historyCount()) {
    Serial.print(F("usage: open <age 0.."));
    Serial.print(workflow.historyCount() ? workflow.historyCount() - 1 : 0);
    Serial.println(F(">  (0 = newest)"));
    return;
  }
  workflow.selectRun((uint8_t)n);
  ui.showScreen(SCR_RESULT);
  ui.renderSerial(Serial);
}

static void cmdScreen(const String &args) {
  if (args.length() > 0) {
    String s = args;
    s.trim();
    s.toLowerCase();
    VisionScreen target = SCR_COUNT;
    if (s == "live") target = SCR_LIVE;
    else if (s == "scan") target = SCR_SCAN;
    else if (s == "checks" || s == "checklist") target = SCR_CHECKS;
    else if (s == "result") target = SCR_RESULT;
    else if (s == "history") target = SCR_HISTORY;
    if (target == SCR_COUNT) {
      Serial.println(F("usage: screen [live|scan|checks|result|history]"));
      return;
    }
    ui.showScreen(target);
  }
  ui.renderSerial(Serial);
}

static void cmdTouch(const String &) {
  ui.printTouchDiagnostics(Serial);
}

static void cmdSelfTest(const String &) {
  int pass = 0, fail = 0;
  Serial.println(F("[selftest] Vision Guard mock inspection flow"));
  auto CHECK = [&](bool c, const char *name) {
    Serial.print(F("  ["));
    Serial.print(c ? F("PASS") : F("FAIL"));
    Serial.print(F("] "));
    Serial.println(name);
    if (c) pass++; else fail++;
  };

  const uint8_t before = workflow.historyCount();
  uint32_t rid = workflow.recordScan("SELFTEST-01", true).id;
  CHECK(workflow.historyCount() == before + 1 ||
            workflow.historyCount() == InspectionWorkflow::kHistoryCap,
        "scan appends to history");
  CHECK(workflow.hasCurrent(), "current run selected after scan");
  CHECK(workflow.current().id == rid, "current is the new run");
  CHECK(workflow.current().passed + workflow.current().failed + workflow.current().skipped ==
            workflow.itemCount(),
        "every check resolved to a state");
  CHECK(workflow.current().failStatus == (workflow.current().failed > 0),
        "overall status matches fail count");

  ItemState s0 = workflow.current().items[0];
  workflow.cycleItem(0);
  CHECK(workflow.current().items[0] != s0, "cycleItem advances the state");
  CHECK(workflow.current().passed + workflow.current().failed + workflow.current().skipped ==
            workflow.itemCount(),
        "counts stay consistent after cycle");

  workflow.setItem(1, ITEM_FAIL);
  CHECK(workflow.current().items[1] == ITEM_FAIL, "setItem forces FAIL");
  CHECK(workflow.current().failStatus, "run reads FAIL with a failed item");

  workflow.reEvaluateCurrent();
  CHECK(workflow.current().passed + workflow.current().failed + workflow.current().skipped ==
            workflow.itemCount(),
        "counts stay consistent after re-evaluate");

  uint8_t n = workflow.historyCount();
  if (n > 1) {
    uint32_t oldestId = workflow.runAt(n - 1).id;
    bool ok = workflow.selectRun(n - 1);
    CHECK(ok && workflow.current().id == oldestId, "selectRun re-opens an older run");
  } else {
    CHECK(true, "selectRun (single run) skipped");
  }

  ui.showScreen(SCR_HISTORY);
  CHECK(ui.screen() == SCR_HISTORY && String(ui.screenName()) == "history",
        "screen navigation reaches history");
  ui.showScreen(SCR_RESULT);

  CHECK(String(cameraStubReason()) == "p4-csi-unavailable-in-arduino",
        "camera stub stays honest");
  ui.renderSerial(Serial);
  CHECK(true, "serial parity render executed");

  eventLog.add(fail == 0 ? "Selftest PASS" : "Selftest FAIL");
  Serial.print(F("[selftest] summary: "));
  Serial.print(pass);
  Serial.print(F(" passed, "));
  Serial.print(fail);
  Serial.print(F(" failed -> "));
  Serial.println(fail == 0 ? F("PASS") : F("FAIL"));
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
  ui.begin(&workflow);
  eventLog.add("Vision Guard mock kiosk booted");

  router.begin(Serial, "vision-guard");
  router.on("status", "uptime, heap, flags, camera stub, current run", cmdStatus);
  router.on("history", "list recorded inspection runs, newest first", cmdHistory);
  router.on("scan", "scan [text] - capture a code and run the checklist", cmdScan);
  router.on("check", "check <0-6> [pass|fail|skip] - set/cycle a checklist item", cmdCheck);
  router.on("eval", "re-evaluate the current run's checklist", cmdEval);
  router.on("open", "open <age> - re-open a history run in Result (0=newest)", cmdOpen);
  router.on("screen", "screen [live|scan|checks|result|history] - show/switch", cmdScreen);
  router.on("touch", "print raw + mapped touch coords, tap count, current screen", cmdTouch);
  router.on("selftest", "drive the mock flow end-to-end with PASS/FAIL lines", cmdSelfTest);
}

void loop() {
  router.poll();
  network.maintain();

  CameraStatus status = camera.status();
  ui.setCameraStatus(status, camera.driverName());

  VisionEvent ev = ui.tick();
  switch (ev.type) {
    case VisionEventType::Scan:
      doScan("");
      break;
    case VisionEventType::CycleItem:
      workflow.cycleItem((uint8_t)ev.index);
      ui.markDirty();
      break;
    case VisionEventType::ReEvaluate:
      workflow.reEvaluateCurrent();
      ui.markDirty();
      break;
    case VisionEventType::OpenRun:
      if (workflow.selectRun((uint8_t)ev.index)) ui.showScreen(SCR_RESULT);
      break;
    default:
      break;
  }

  // Background mock scanner: grow the audit log without disturbing a review.
  String qr;
  if (qrScanner.poll(qr)) {
    runInspection(qr, /*makeCurrent=*/false);
  }

  // Small yield only; demo cadence comes from Throttle gates. Worst-case
  // serial command latency is one pass (~20 ms) - imperceptible.
  delay(20);
}
