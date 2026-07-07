#include "config/ProjectConfig.h"
#include "src/BleUartBridge.h"
#include "src/QrStateStore.h"
#include "src/ScanLog.h"
#include "src/WifiScanner.h"
#include <CrowPanelShared.h>

#if USE_BLE_UART_BRIDGE
#include <HardwareSerial.h>
HardwareSerial bleBridgeSerial(CYPHERDRIVE_BLE_UART_PORT);
#endif

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
WifiScanner wifiScanner;
BleUartBridge bleBridge;
QrStateStore qrStore;
ScanLog scanLog;

uint16_t scanRuns = 0;

String flagState() {
  return String("wifi_scan=") + (USE_WIFI_SCAN ? "on" : "off") +
         " ble_uart=" + (USE_BLE_UART_BRIDGE ? "on" : "off") +
         " qr_persistence=" + (USE_QR_PERSISTENCE ? "on" : "off") +
         " display=" + (USE_DISPLAY ? "on" : "off");
}

String wifiDetail(WifiNetworkRecord records[], uint8_t count) {
  if (count == 0) return "No Wi-Fi records|No join, credential, or capture path";

  String detail;
  uint8_t shown = count < 4 ? count : 4;
  for (uint8_t i = 0; i < shown; ++i) {
    if (i > 0) detail += "|";
    detail += records[i].ssid + " ch" + String(records[i].channel) + " " +
              String(records[i].rssi) + " dBm " + records[i].auth;
  }
  detail += "|Visibility only: no join, portal, deauth, HID, or capture";
  return detail;
}

String bleDetail(BleAdvertisementRecord records[], uint8_t count) {
  if (count == 0) return "No BLE bridge frames|Use sidecar UART or bridge smoke command";

  String detail;
  uint8_t shown = count < 4 ? count : 4;
  for (uint8_t i = 0; i < shown; ++i) {
    if (i > 0) detail += "|";
    detail += records[i].label + " " + String(records[i].rssi) + " dBm " +
              records[i].vendor;
  }
  detail += "|Parser only: no BLE stack or active control on the panel";
  return detail;
}

void refreshDashboard(const String &focus) {
  dashboard.setTile(0, "Wi-Fi Scan", String(scanLog.countType(ScanLog::kWifi)) + " nets",
                    wifiScanner.driverName());
  dashboard.setTile(1, "BLE Bridge", String(scanLog.countType(ScanLog::kBle)) + " adv",
                    bleBridge.driverName());
  dashboard.setTile(2, "QR Link", qrStore.persistenceEnabled() ? "saved" : "volatile",
                    qrStore.url());
  dashboard.setTile(3, "Scan Log", String(scanLog.count()) + " items", scanLog.latestSummary());
  dashboard.setBanner(focus);
  dashboard.setFooter("Visibility only: no HID, captive portal, credential capture, or external mutation");
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypherdrive-ops", storage.eventCount());
  Serial.print(F("[cypherdrive] flags "));
  Serial.println(flagState());
  Serial.print(F("[cypherdrive] scan_runs="));
  Serial.println(scanRuns);
  Serial.print(F("[cypherdrive] qr_mode="));
  Serial.println(qrStore.persistenceEnabled() ? F("preferences") : F("volatile"));
  Serial.println(F("[cypherdrive] safety=no HID, no captive portal, no credential capture, no external mutation"));
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void logWifiRecords(WifiNetworkRecord records[], uint8_t count, const char *source) {
  if (count == 0) {
    scanLog.recordInfo("wifi", "no networks", String("source=") + source);
    eventLog.add("Wi-Fi scan returned no records");
    dashboard.setDetail("Wi-Fi Scan", wifiDetail(records, 0));
    refreshDashboard("Wi-Fi scan returned no records");
    return;
  }

  for (uint8_t i = 0; i < count; ++i) {
    scanLog.recordWifi(records[i], source);
  }
  eventLog.add("Wi-Fi visibility scan complete");
  dashboard.setDetail("Wi-Fi Scan", wifiDetail(records, count));
  refreshDashboard("Wi-Fi scan complete");
}

void logBleRecords(BleAdvertisementRecord records[], uint8_t count, const char *source,
                   const String &focus) {
  if (count == 0) {
    scanLog.recordInfo("ble", "no advertisements", String("source=") + source);
    eventLog.add("BLE scan returned no records");
    dashboard.setDetail("BLE Bridge", bleDetail(records, 0));
    refreshDashboard(focus);
    return;
  }

  for (uint8_t i = 0; i < count; ++i) {
    scanLog.recordBle(records[i], source);
  }
  eventLog.add("BLE visibility scan complete");
  dashboard.setDetail("BLE Bridge", bleDetail(records, count));
  refreshDashboard(focus);
}

void cmdScan(const String &args) {
  String mode = args;
  mode.trim();
  mode.toLowerCase();
  ++scanRuns;

  if (mode == "ble") {
    BleAdvertisementRecord records[4];
    uint8_t count = bleBridge.scan(records, 4, Serial);
    logBleRecords(records, count, bleBridge.driverName(), "BLE scan complete");
    return;
  }

  if (mode.length() == 0 || mode == "wifi") {
    WifiNetworkRecord records[CYPHERDRIVE_WIFI_SCAN_MAX_RESULTS];
    uint8_t count = wifiScanner.scan(records, CYPHERDRIVE_WIFI_SCAN_MAX_RESULTS, Serial);
    logWifiRecords(records, count, wifiScanner.driverName());
    return;
  }

  Serial.println(F("[scan] usage: scan wifi | scan ble"));
  scanLog.recordInfo("scan", "invalid scan mode", mode);
  refreshDashboard("scan command needs wifi or ble");
}

void cmdBridge(const String &args) {
  String line = args;
  line.trim();
  if (line.length() == 0) {
    Serial.println(F("[ble-bridge] usage: bridge BLE,<label>,<addr>,<rssi>,<vendor>,<note>"));
    return;
  }

  BleAdvertisementRecord record;
  if (bleBridge.injectLine(line, record, Serial)) {
    BleAdvertisementRecord records[1];
    records[0] = record;
    scanLog.recordBle(record, "serial-smoke");
    eventLog.add("BLE bridge smoke frame parsed");
    dashboard.setDetail("BLE Bridge", bleDetail(records, 1));
    refreshDashboard("BLE bridge frame parsed");
  }
}

void cmdQr(const String &args) {
  String rest = args;
  rest.trim();
  if (rest.startsWith("set ")) {
    String nextUrl = rest.substring(4);
    if (qrStore.setUrl(nextUrl, Serial)) {
      eventLog.add("QR link updated");
      scanLog.recordQr(qrStore.url(), qrStore.persistenceEnabled());
    } else {
      eventLog.add("QR link rejected");
    }
  } else if (rest.length() > 0 && rest != "show") {
    Serial.println(F("[qr] usage: qr show | qr set <url>"));
  }
  Serial.print(F("[qr] "));
  Serial.print(qrStore.url());
  Serial.print(F(" mode="));
  Serial.println(qrStore.persistenceEnabled() ? F("preferences") : F("volatile"));
  dashboard.setDetail("QR Link", String("Current handoff URL|") + qrStore.url() +
                                   "|Shown as text until QR renderer is added");
  refreshDashboard("QR handoff ready");
}

void cmdLogs(const String &) {
  scanLog.print(Serial);
  eventLog.add("Scan log printed");
  dashboard.setDetail("Scan Log", String("entries=") + scanLog.count() +
                                  "|latest=" + scanLog.latestSummary() +
                                  "|state is local to the panel demo");
  refreshDashboard("Scan log listed");
}

void pollBleBridge() {
#if USE_BLE_UART_BRIDGE
  BleAdvertisementRecord records[2];
  uint8_t count = bleBridge.readAvailable(records, 2, Serial);
  if (count > 0) {
    logBleRecords(records, count, "uart-sidecar", "BLE bridge frame received");
  }
#endif
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CypherDrive Wireless Ops");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypherdrive");
  wifiScanner.begin();
  qrStore.begin("cypherdrive", CYPHERDRIVE_DEFAULT_QR_URL);

#if USE_BLE_UART_BRIDGE
  bleBridgeSerial.begin(CYPHERDRIVE_BLE_UART_BAUD, SERIAL_8N1,
                        CYPHERDRIVE_BLE_UART_RX_PIN, CYPHERDRIVE_BLE_UART_TX_PIN);
  if (CYPHERDRIVE_BLE_UART_RX_PIN < 0 || CYPHERDRIVE_BLE_UART_TX_PIN < 0) {
    Logger::warn("ble-bridge", "UART pins unset; define CYPHERDRIVE_BLE_UART_RX_PIN/TX_PIN in config/Pins.h");
  }
  bleBridge.begin(&bleBridgeSerial);
#else
  bleBridge.begin(nullptr);
#endif

  dashboard.begin("CYPHERDRIVE", "WIRELESS OPS", "SAFE MOCK");
  refreshDashboard("ready for passive wireless visibility");
  dashboard.setDetail("Wireless Ops", "Run scan wifi or scan ble|Use logs for scan state|No active tools in v1");
  eventLog.add("CypherDrive Wireless Ops booted");
  scanLog.recordInfo("boot", "wireless ops ready", flagState());
  router.begin(Serial, "cypherdrive");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "scan wifi|ble - safe visibility scan", cmdScan);
  router.on("bridge", "bridge BLE,<label>,<addr>,<rssi>,<vendor>,<note>", cmdBridge);
  router.on("qr", "qr set <url> or qr show", cmdQr);
  router.on("logs", "print scan log state", cmdLogs);
}

void loop() {
  router.poll();
  pollBleBridge();
  dashboard.tick();
  delay(20);
}
