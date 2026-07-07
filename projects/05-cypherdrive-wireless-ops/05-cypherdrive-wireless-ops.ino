#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

String qrUrl = "https://techtiff.ai/cypher-drive";
uint16_t scanCount = 0;

void refreshDashboard(const String &focus) {
  dashboard.setTile(0, "Wi-Fi Scan", String(scanCount) + " nets", "passive station scan");
  dashboard.setTile(1, "BLE Scan", "12 dev", "nearby advertisements");
  dashboard.setTile(2, "QR Link", "ready", qrUrl);
  dashboard.setTile(3, "Logs", "3 files", "mock session archive");
  dashboard.setBanner(focus);
  dashboard.setFooter("CypherDrive port: visibility only, no HID or capture flows in v1");
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypherdrive-ops", storage.eventCount());
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdScan(const String &args) {
  String mode = args;
  mode.trim();
  scanCount++;
  if (mode == "ble") {
    Serial.println(F("[scan:ble] AirPods-Pro rssi=-58 vendor=Apple"));
    Serial.println(F("[scan:ble] SensorTag rssi=-71 vendor=TI"));
    eventLog.add("BLE visibility scan complete");
    dashboard.setDetail("BLE Scan", "AirPods-Pro -58 dBm|SensorTag -71 dBm|Find My-like frames: mock only");
    refreshDashboard("BLE scan loaded from mock data");
    return;
  }
  Serial.println(F("[scan:wifi] StudioNet ch=6 rssi=-42 wpa2"));
  Serial.println(F("[scan:wifi] GuestLab ch=11 rssi=-67 open"));
  eventLog.add("Wi-Fi visibility scan complete");
  dashboard.setDetail("Wi-Fi Scan", "StudioNet ch6 -42 dBm|GuestLab ch11 -67 dBm|No join, capture, or attack path");
  refreshDashboard("Wi-Fi scan loaded from mock data");
}

void cmdQr(const String &args) {
  String rest = args;
  rest.trim();
  if (rest.startsWith("set ")) {
    qrUrl = rest.substring(4);
    qrUrl.trim();
    eventLog.add("QR link updated");
  }
  Serial.print(F("[qr] "));
  Serial.println(qrUrl);
  dashboard.setDetail("QR Link", String("Current handoff URL|") + qrUrl + "|Shown as text until QR renderer is added");
  refreshDashboard("QR handoff ready");
}

void cmdLogs(const String &) {
  Serial.println(F("[logs] wifi_001.csv ble_001.csv session_summary.txt"));
  eventLog.add("Log browser opened");
  dashboard.setDetail("Logs", "wifi_001.csv|ble_001.csv|session_summary.txt");
  refreshDashboard("Mock session logs listed");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CypherDrive Wireless Ops");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypherdrive");
  dashboard.begin("CYPHERDRIVE", "WIRELESS OPS", "SAFE MOCK");
  refreshDashboard("ready for passive wireless visibility");
  dashboard.setDetail("Wireless Ops", "Run scan wifi or scan ble|Touch cards to inspect them|No active tools in v1");
  eventLog.add("CypherDrive Wireless Ops booted");
  router.begin(Serial, "cypherdrive");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "scan wifi|ble - load mock visibility data", cmdScan);
  router.on("qr", "qr set <url> or qr show", cmdQr);
  router.on("logs", "list mock scan logs", cmdLogs);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
