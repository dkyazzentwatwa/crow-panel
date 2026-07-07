#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
String preset = "433 ISM";
uint16_t rows = 0;
uint16_t peak = 0;
bool scanning = false;

void refreshRf(const String &banner) {
  dashboard.setTile(0, "Preset", preset, "receive-only");
  dashboard.setTile(1, "Scan", scanning ? "RUN" : "STOP", "mock rows");
  dashboard.setTile(2, "Rows", String(rows), "heatmap history");
  dashboard.setTile(3, "Peak", String(peak), "relative power");
  dashboard.setTile(4, "Power", "RAW", "uncalibrated");
  dashboard.setTile(5, "Safety", "RX only", "no TX controls");
  dashboard.setBanner(banner);
  dashboard.setFooter("CardRF v1 is receive-only mock data; no TX/replay/jamming controls");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "cardrf-spectrum", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdScan(const String &) {
  scanning = true;
  rows++;
  peak = 188;
  Serial.println(F("[scan] EVENT SCANROW START=433000000 STEP=100000 BINS=16 MIN=12 MAX=188 DATA=1028446688AACCEE"));
  eventLog.add("Mock RF scan row");
  dashboard.setDetail("Spectrum Row", String("Preset ") + preset + "|Rows " + String(rows) + "|Peak relative power " + String(peak));
  refreshRf("mock scan row rendered");
}

void cmdFeed(const String &args) {
  rows++;
  peak = 210;
  Serial.println(String("[feed] ") + args);
  dashboard.setDetail("Fed SCANROW", String("Rows ") + String(rows) + "|Peak updated to " + String(peak) + "|Input retained as Serial proof");
  refreshRf("SCANROW feed accepted");
}

void cmdPower(const String &) {
  peak = 142;
  Serial.println(F("[power] RAW=142 CLIP=0 SAMPLES=128"));
  dashboard.setDetail("Power", "RAW 142|CLIP 0|Uncalibrated receive-strength estimate");
  refreshRf("power sample read");
}

void cmdPreset(const String &args) {
  preset = args;
  preset.trim();
  if (preset.length() == 0) preset = "433 ISM";
  Serial.println(String("[preset] ") + preset);
  dashboard.setDetail("Preset", preset + "|Receive-only sweep profile|Real UART bridge is future gated work");
  refreshRf("preset changed");
}

void cmdStop(const String &) {
  scanning = false;
  Serial.println(F("[scan] stopped"));
  eventLog.add("RF scan stopped");
  dashboard.setDetail("Stopped", "Scan stopped|No RF hardware command sent in v1");
  refreshRf("scan stopped");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CardRF Spectrum Console");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cardrf");
  dashboard.begin("CARDRF", "SPECTRUM CONSOLE", "RX MOCK");
  refreshRf("receive-only spectrum console ready");
  eventLog.add("CardRF Spectrum booted");
  router.begin(Serial, "cardrf");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "generate mock SCANROW", cmdScan);
  router.on("feed", "feed <SCANROW...>", cmdFeed);
  router.on("power", "mock POWER? response", cmdPower);
  router.on("preset", "preset <name>", cmdPreset);
  router.on("stop", "stop mock scan", cmdStop);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
