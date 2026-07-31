#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/MockLoRaGateway.h"
#include "src/EspNowGateway.h"
#include "src/AlertEngine.h"
#include "src/AiSummaryClient.h"
#include "src/FieldOpsUi.h"

// Transport selection (compile-time). ESP-NOW reads bridged mesh frames over
// UART; LoRa drives a real SX1262; mock generates synthetic packets. All feed
// the same processPacket() below.
#if USE_ESPNOW
EspNowGateway gateway;
#elif USE_LORA_DRIVER
LoRaGateway gateway;  // real SX1262 via RadioLib - compile-verified, not hardware-verified
#else
MockLoRaGateway gateway;
#endif
AlertEngine alerts;
AiSummaryClient summaries;
FieldOpsUi ui;
EventLog eventLog;
StorageManager storage;
CrowNetworkClient network;
SerialCommandRouter router;

// One pipeline for every packet source: the mock gateway, the serial
// `inject` command, and (once USE_LORA_DRIVER is hardware-verified) the
// real SX1262 driver all feed this same function.
void processPacket(const SensorPacket &packet) {
  ui.renderDashboard(packet);

  // Presence-only frames (a chat node's heartbeat) carry no telemetry: update
  // the roster and stop - no thresholds, summary, or backend POST.
  if (packet.presenceOnly) {
    Logger::diag("fieldops_tick", "presence", "node=" + packet.nodeId);
    return;
  }

  String alert = alerts.evaluate(packet);
  if (alert.length() > 0) {
    eventLog.add(alert);
    ui.renderAlert(alert);
  }

  storage.incrementEventCount();
  // Mock JSON, unescaped - swap for real serialization (ArduinoJson)
  // before a backend ingests this for real.
  network.postEvent(String("{\"source\":\"fieldops\",\"node\":\"") + packet.nodeId + "\"}");

  String summary = summaries.summarize(packet, alert);
  ui.renderSummary(summary);

  Logger::diag("fieldops_tick", "ok", "events=" + String(storage.eventCount()));
}

// Pops the first space-separated word off `line` and returns it.
String nextWord(String &line) {
  line.trim();
  int space = line.indexOf(' ');
  if (space < 0) {
    String word = line;
    line = "";
    return word;
  }
  String word = line.substring(0, space);
  line = line.substring(space + 1);
  return word;
}

void cmdInject(const String &args) {
  String rest = args;
  String nodeWord = nextWord(rest);
  String tempWord = nextWord(rest);
  String battWord = nextWord(rest);

  uint8_t node = nodeWord.length() > 0 ? (uint8_t)constrain(nodeWord.toInt(), 0, 3) : 0;
  SensorPacket packet = SensorNode::makeMock(node);
  if (tempWord.length() > 0) {
    packet.temperatureC = tempWord.toFloat();
  }
  if (battWord.length() > 0) {
    packet.batteryPct = battWord.toFloat();
  }

  Logger::info("cmd", "injecting packet from " + packet.nodeId);
  processPacket(packet);
}

// Bench-test the ESP-NOW path with no bridge: inject a raw CSV frame exactly
// as the bridge would send it over UART.
void cmdFeed(const String &args) {
  SensorPacket packet;
  if (SensorNode::parseCsvFrame(args, packet)) {
    Logger::info("cmd", "feed " + packet.nodeId);
    processPacket(packet);
  } else {
    Logger::warn("cmd", "bad frame; use: feed SENSOR,name,tempC,hum,batt,motion,rssi  or  feed PRESENCE,name,rssi,type");
  }
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "fieldops", storage.eventCount(), &router);
  const FieldOpsDashboard &d = ui.dashboard();
  Serial.print(F("[fieldops] screen="));
  Serial.print(d.screenName());
  Serial.print(F(" nodes="));
  Serial.print(d.nodeCount());
  Serial.print(F(" active="));
  Serial.print(d.activeNodeName());
  Serial.print(F(" alerts="));
  Serial.print(d.alertCount());
  Serial.print(F(" unacked="));
  Serial.print(d.unackedAlertCount());
  Serial.print(F(" log="));
  Serial.print(d.logCount());
  Serial.print(F(" page="));
  Serial.print(d.logPage() + 1);
  Serial.print(F("/"));
  Serial.println(d.logPageCount());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

// Executes the typed event tick() returns from a touch action, keeping the
// shared EventLog authoritative (the UI never writes it directly).
void handleUiEvent(const FieldOpsUiEvent &ev) {
  switch (ev.type) {
    case FIELDOPS_EVT_PIN_NODE:
      Logger::info("ui", String("pinned ") + ev.label);
      eventLog.add(String("Pinned ") + ev.label);
      ui.note(String("Pinned ") + ev.label);
      break;
    case FIELDOPS_EVT_ACK_ALERT:
      Logger::info("ui", String("ack ") + ev.label);
      eventLog.add(String("ACK ") + ev.label);
      ui.note(String("ACK ") + ev.label);
      break;
    default:
      break;
  }
}

// `touch` - raw + mapped touch coordinates, tap count, and current screen.
void cmdTouch(const String &) { ui.printTouch(Serial); }

// `screen <roster|detail|alerts|log>` - the touch tab bar, over serial.
void cmdScreen(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  FieldOpsScreen s;
  if (a.startsWith("roster")) s = FIELDOPS_SCR_ROSTER;
  else if (a.startsWith("detail")) s = FIELDOPS_SCR_DETAIL;
  else if (a.startsWith("alert")) s = FIELDOPS_SCR_ALERTS;
  else if (a.startsWith("log")) s = FIELDOPS_SCR_LOG;
  else {
    Serial.println(F("[screen] usage: screen <roster|detail|alerts|log>"));
    return;
  }
  ui.setScreen(s);
  Serial.print(F("[screen] now "));
  Serial.println(ui.dashboard().screenName());
}

// `pin <node-name|index>` - the same as tapping a roster card.
void cmdPin(const String &args) {
  String a = args;
  a.trim();
  if (a.length() == 0) {
    Serial.println(F("[pin] usage: pin <node-name|index 0-7>"));
    return;
  }
  bool numeric = true;
  for (uint16_t i = 0; i < a.length(); i++) {
    if (!isDigit(a[i])) { numeric = false; break; }
  }
  int8_t idx;
  if (numeric) {
    int8_t n = (int8_t)a.toInt();
    idx = ui.pinNodeByIndex(n) ? n : -1;
  } else {
    idx = ui.pinNodeByName(a);
  }
  if (idx < 0) {
    Serial.print(F("[pin] no node "));
    Serial.println(a);
    return;
  }
  ui.setScreen(FIELDOPS_SCR_DETAIL);
  String name = ui.dashboard().activeNodeName();
  eventLog.add(String("Pinned ") + name);
  ui.note(String("Pinned ") + name);
  Serial.print(F("[pin] pinned "));
  Serial.println(name);
}

// `ack [n]` - acknowledge the newest unacked alert, or the nth on-screen row.
void cmdAck(const String &args) {
  String a = args;
  a.trim();
  int8_t ring = (a.length() == 0) ? ui.ackNewestAlert()
                                  : ui.ackAlertByDisplay((uint8_t)a.toInt());
  if (ring < 0) {
    Serial.println(F("[ack] no matching unacked alert"));
    return;
  }
  eventLog.add("ACK alert");
  ui.note("ACK alert");
  Serial.print(F("[ack] acknowledged alert (slot "));
  Serial.print(ring);
  Serial.println(F(")"));
}

// `log <prev|next|N>` - page the on-screen event log.
void cmdLog(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  if (a == "prev" || a == "newer" || a == "up") {
    ui.logPagePrev();
  } else if (a == "next" || a == "older" || a == "down") {
    ui.logPageNext();
  } else if (a.length() > 0 && a != "show") {
    ui.setLogPage((uint16_t)a.toInt());
  }
  ui.setScreen(FIELDOPS_SCR_LOG);
  const FieldOpsDashboard &d = ui.dashboard();
  Serial.print(F("[log] page "));
  Serial.print(d.logPage() + 1);
  Serial.print(F("/"));
  Serial.print(d.logPageCount());
  Serial.print(F(" ("));
  Serial.print(d.logCount());
  Serial.println(F(" events)"));
}

// `selftest` - drive the whole mock flow headlessly with explicit PASS/FAIL.
void cmdSelfTest(const String &) {
  Serial.println(F("[selftest] FieldOps mock flow"));
  int pass = 0;
  int fail = 0;
  auto check = [&](const char *name, bool ok) {
    Serial.print(ok ? F("[selftest] PASS ") : F("[selftest] FAIL "));
    Serial.println(name);
    if (ok) pass++;
    else fail++;
  };

  // 1) A telemetry packet populates the roster and the rolling log. Uses the
  // monotonic push counter so the check holds even once the log ring is full.
  uint32_t pushesBefore = ui.dashboard().logPushes();
  SensorPacket p0 = SensorNode::makeMock(0);
  p0.temperatureC = 22.5f;
  p0.batteryPct = 80.0f;
  p0.motion = false;
  processPacket(p0);
  check("roster node registered", ui.dashboard().nodeCount() >= 1);
  check("rolling log grew", ui.dashboard().logPushes() > pushesBefore);

  // 2) A low-battery packet leaves an unacknowledged critical alert in the
  // stream (whether a fresh entry or a refreshed duplicate).
  SensorPacket p1 = SensorNode::makeMock(1);
  p1.batteryPct = 9.0f;  // below the 35% LOW_BATTERY threshold
  processPacket(p1);
  check("alert raised on low battery", ui.dashboard().unackedAlertCount() >= 1);

  // 3) Acknowledge the newest alert.
  uint8_t unackedMid = ui.dashboard().unackedAlertCount();
  int8_t acked = ui.ackNewestAlert();
  check("alert acknowledged",
        acked >= 0 && ui.dashboard().unackedAlertCount() < unackedMid);

  // 4) Pin a node by name (the roster-tap equivalent).
  int8_t pinned = ui.pinNodeByName(p0.nodeId);
  check("node pinned by name",
        pinned >= 0 && ui.dashboard().activeNodeIndex() == pinned);

  // 5) Screen navigation across all four tabs.
  bool navOk = ui.setScreen(FIELDOPS_SCR_DETAIL) &&
               ui.dashboard().screen() == FIELDOPS_SCR_DETAIL;
  navOk = navOk && ui.setScreen(FIELDOPS_SCR_ALERTS) &&
          ui.dashboard().screen() == FIELDOPS_SCR_ALERTS;
  navOk = navOk && ui.setScreen(FIELDOPS_SCR_LOG) &&
          ui.dashboard().screen() == FIELDOPS_SCR_LOG;
  check("screen navigation", navOk);

  // 6) Event-log paging is consistent with the page count.
  uint16_t pages = ui.dashboard().logPageCount();
  ui.setLogPage(0);
  ui.logPageNext();
  bool pageOk = pages >= 1 && (pages <= 1 ? ui.dashboard().logPage() == 0
                                          : ui.dashboard().logPage() == 1);
  check("event-log paging", pageOk);

  ui.setScreen(FIELDOPS_SCR_ROSTER);
  Serial.print(F("[selftest] SUMMARY "));
  Serial.print(pass);
  Serial.print(F(" passed, "));
  Serial.print(fail);
  Serial.println(F(" failed"));
  eventLog.add(fail == 0 ? "Selftest PASS" : "Selftest FAIL");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel FieldOps Control Center");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("fieldops");
  network.begin(FIELDOPS_API_ENDPOINT, WIFI_SSID, WIFI_PASS);
  gateway.begin(profile);
  summaries.begin(&network);
  ui.begin();
  eventLog.add("FieldOps mock dashboard booted");

  router.begin(Serial, "fieldops");
  router.on("status", "uptime, heap, profile, flags, UI state", cmdStatus);
  router.on("history", "recent events, oldest first", cmdHistory);
  router.on("inject", "inject [node 0-3] [tempC] [batteryPct] - simulate a packet, e.g. inject 1 40 12", cmdInject);
  router.on("feed", "feed a bridge CSV frame, e.g. feed SENSOR,ATTIC,29.5,40,88,0,-58", cmdFeed);
  router.on("screen", "screen <roster|detail|alerts|log> - switch screen", cmdScreen);
  router.on("pin", "pin <node-name|index> - pin a node to Detail", cmdPin);
  router.on("ack", "ack [n] - acknowledge newest (or nth) alert", cmdAck);
  router.on("log", "log <prev|next|N> - page the event log", cmdLog);
  router.on("touch", "raw+mapped touch coords, tap count, current screen", cmdTouch);
  router.on("selftest", "drive the mock flow headlessly, print PASS/FAIL", cmdSelfTest);
}

void loop() {
  router.poll();
  network.maintain();

  FieldOpsUiEvent event;
  if (ui.tick(event)) {
    handleUiEvent(event);
  }

  SensorPacket packet;
  if (gateway.poll(packet)) {
    processPacket(packet);
  }

  // Small yield only; demo cadence comes from Throttle gates. Worst-case
  // serial command latency is one pass (~20 ms) - imperceptible.
  delay(20);
}
