#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/CardRfParser.h"
#include "src/CardRfSpectrumState.h"
#include "src/RfUartBridge.h"

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
CardRfParser parser;
CardRfSpectrumState spectrum;
RfUartBridge bridge;

void refreshRf(const String &banner) {
  dashboard.setTile(0, "Preset", spectrum.preset(), "receive-only");
  dashboard.setTile(1, "Scan", spectrum.scanLabel(), spectrum.sourceLabel());
  dashboard.setTile(2, "Rows", spectrum.rowsLabel(), "heatmap history");
  dashboard.setTile(3, "Peak", spectrum.peakLabel(), "relative power");
  dashboard.setTile(4, "Power", spectrum.powerLabel(), "uncalibrated");
  dashboard.setTile(5, "Heatmap", spectrum.heatmapLabel(), "latest bins");
  dashboard.setTile(6, "Bridge", spectrum.bridgeLabel(USE_RF_UART_BRIDGE), bridge.status());
  dashboard.setTile(7, "Safety", "RX only", "no TX controls");
  dashboard.setBanner(banner);
  dashboard.setFooter("CardRF is receive-only; no TX, replay, jamming, or mutation controls");
}

void cmdHistory(const String &) { eventLog.printHistory(Serial); }

bool ingestCardRfLine(const String &line, const String &source) {
  CardRfLine parsed;
  String error;
  if (!parser.parseLine(line, parsed, error)) {
    Serial.println(String("[cardrf] parse_error source=") + source + " error=\"" + error + "\"");
    return false;
  }

  if (parsed.kind == CARD_RF_LINE_SCANROW) {
    spectrum.applyScanRow(parsed.scanRow, source);
    eventLog.add(String("SCANROW from ") + source);
    storage.incrementEventCount();
    Serial.println(String("[cardrf] SCANROW rows=") + spectrum.rowsLabel() +
                   " peak=" + spectrum.peakLabel() + " source=" + source);
    dashboard.setDetail("Spectrum Row", spectrum.scanDetail());
    refreshRf(String("SCANROW parsed from ") + source);
    return true;
  }

  if (parsed.kind == CARD_RF_LINE_POWER) {
    spectrum.applyPower(parsed.power, source);
    eventLog.add(String("POWER from ") + source);
    storage.incrementEventCount();
    Serial.println(String("[cardrf] POWER ") + spectrum.powerLabel() + " source=" + source);
    dashboard.setDetail("Power", spectrum.powerDetail());
    refreshRf(String("POWER parsed from ") + source);
    return true;
  }

  return false;
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "cardrf-spectrum", storage.eventCount());
  Serial.println(String("[cardrf] ") + spectrum.statusDetail());
  Serial.println(String("[cardrf] bridge=") + bridge.status());
}

void cmdScan(const String &) {
  const String line = "SCANROW START=433000000 STEP=100000 BINS=16 MIN=12 MAX=188 DATA=1028446688AACCEE";
  Serial.println(String("[scan] ") + line);
  ingestCardRfLine(line, "mock-scan");
}

void cmdFeed(const String &args) {
  if (args.length() == 0) {
    Serial.println(F("[feed] expected SCANROW ... or POWER ..."));
    return;
  }
  Serial.println(String("[feed] ") + args);
  ingestCardRfLine(args, "serial-feed");
}

void cmdPower(const String &) {
  const String line = "POWER RAW=142 CLIP=0 SAMPLES=128";
  Serial.println(String("[power] ") + line);
  ingestCardRfLine(line, "mock-power");
}

void cmdPreset(const String &args) {
  spectrum.setPreset(args);
  Serial.println(String("[preset] ") + spectrum.preset());
  dashboard.setDetail("Preset", spectrum.preset() + "|Receive-only sweep profile|UART bridge ingestion is RX-only");
  refreshRf("preset changed");
}

void cmdStop(const String &) {
  spectrum.stop();
  Serial.println(F("[scan] stopped"));
  eventLog.add("RF scan stopped");
  dashboard.setDetail("Stopped", "Scan stopped locally|No RF hardware command sent");
  refreshRf("scan stopped");
}

void cmdHeatmap(const String &) {
  Serial.println(String("[heatmap] ") + spectrum.scanDetail());
  dashboard.setDetail("Heatmap", spectrum.scanDetail());
  refreshRf("heatmap status shown");
}

void cmdBridge(const String &) {
  Serial.println(String("[bridge] ") + bridge.status());
  dashboard.setDetail("Bridge", bridge.status() + "|Ingests SCANROW/POWER only|Never writes RF commands");
  refreshRf("bridge status shown");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CardRF Spectrum Console");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cardrf");
  spectrum.begin("433 ISM");
  bridge.begin();
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
  router.on("heatmap", "show latest heatmap bins", cmdHeatmap);
  router.on("bridge", "show RX UART bridge status", cmdBridge);
}

void loop() {
  router.poll();
  String bridgeLine;
  if (bridge.poll(bridgeLine)) {
    ingestCardRfLine(bridgeLine, "uart-bridge");
  }
  dashboard.tick();
  delay(20);
}
