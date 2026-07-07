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
  printSystemStatus(Serial, "fieldops", storage.eventCount());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
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
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events, oldest first", cmdHistory);
  router.on("inject", "inject [node 0-3] [tempC] [batteryPct] - simulate a packet, e.g. inject 1 40 12", cmdInject);
  router.on("feed", "feed a bridge CSV frame, e.g. feed SENSOR,ATTIC,29.5,40,88,0,-58", cmdFeed);
}

void loop() {
  router.poll();
  network.maintain();
  ui.tick();

  SensorPacket packet;
  if (gateway.poll(packet)) {
    processPacket(packet);
  }

  // Small yield only; demo cadence comes from Throttle gates. Worst-case
  // serial command latency is one pass (~20 ms) - imperceptible.
  delay(20);
}
