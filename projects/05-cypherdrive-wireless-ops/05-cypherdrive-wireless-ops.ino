#include "config/ProjectConfig.h"
#include "src/BleUartBridge.h"
#include "src/QrStateStore.h"
#include "src/ScanLog.h"
#include "src/WifiScanner.h"
#include "src/WirelessOpsUi.h"
#include <CrowPanelShared.h>

// Project 05 - CypherDrive Wireless Ops.
// A passive wireless-visibility console: a touch-first, four-screen UI
// (WI-FI / BLE / LOG / QR) drawn with the shared Widgets toolkit, plus a
// full Serial command surface with 1:1 parity. Deliberately look-don't-touch:
// no joining, injection, deauth, HID, or capture. Mock-first - every screen
// fills with believable data with no radio hardware attached.

#if USE_BLE_UART_BRIDGE
#include <HardwareSerial.h>
HardwareSerial bleBridgeSerial(CYPHERDRIVE_BLE_UART_PORT);
#endif

WirelessOpsUi ui;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
WifiScanner wifiScanner;
BleUartBridge bleBridge;
QrStateStore qrStore;
ScanLog scanLog;

WifiNetworkRecord wifiRows[WirelessOpsUi::kMaxWifi];
uint8_t wifiCount = 0;
BleAdvertisementRecord bleRows[WirelessOpsUi::kMaxBle];
uint8_t bleCount = 0;
uint16_t scanRuns = 0;

String flagState() {
  return String("wifi_scan=") + (USE_WIFI_SCAN ? "on" : "off") +
         " ble_uart=" + (USE_BLE_UART_BRIDGE ? "on" : "off") +
         " qr_persistence=" + (USE_QR_PERSISTENCE ? "on" : "off") +
         " display=" + (USE_DISPLAY ? "on" : "off");
}

// --- UI sync helpers. The UI owns rendering only; the sketch pushes snapshots
// and never lets the UI mutate application state. ---
void syncWifi() { ui.setWifi(wifiRows, wifiCount); }
void syncBle() { ui.setBle(bleRows, bleCount); }
void syncLog() { ui.setLog(&scanLog); }  // re-point (same ptr) + mark dirty
void syncQr() { ui.setQr(qrStore.url(), qrStore.persistenceEnabled(), CYPHERDRIVE_DEFAULT_QR_URL); }

// --- Scan flows (shared by touch buttons and the `scan` serial command). ---
void runWifiScan() {
  ++scanRuns;
  const char *source = wifiScanner.driverName();
  wifiCount = wifiScanner.scan(wifiRows, WirelessOpsUi::kMaxWifi, Serial);
  if (wifiCount == 0) {
    scanLog.recordInfo("wifi", "no networks", String("source=") + source);
    eventLog.add("Wi-Fi scan returned no records");
  } else {
    for (uint8_t i = 0; i < wifiCount; ++i) scanLog.recordWifi(wifiRows[i], source);
    eventLog.add("Wi-Fi visibility scan complete");
  }
  syncWifi();
  syncLog();
  ui.setStatus(String("wifi: ") + wifiCount + " nets");
}

void runBleScan() {
  ++scanRuns;
  const char *source = bleBridge.driverName();
  bleCount = bleBridge.scan(bleRows, WirelessOpsUi::kMaxBle, Serial);
  if (bleCount == 0) {
    scanLog.recordInfo("ble", "no advertisements", String("source=") + source);
    eventLog.add("BLE scan returned no records");
  } else {
    for (uint8_t i = 0; i < bleCount; ++i) scanLog.recordBle(bleRows[i], source);
    eventLog.add("BLE visibility scan complete");
  }
  syncBle();
  syncLog();
  ui.setStatus(String("ble: ") + bleCount + " adv");
}

// Prepend one parsed advertisement so the newest sits at the top of the list.
void ingestBleRecord(const BleAdvertisementRecord &record, const char *source) {
  uint8_t shifted = bleCount < WirelessOpsUi::kMaxBle ? bleCount : (uint8_t)(WirelessOpsUi::kMaxBle - 1);
  for (uint8_t i = shifted; i > 0; --i) bleRows[i] = bleRows[i - 1];
  bleRows[0] = record;
  if (bleCount < WirelessOpsUi::kMaxBle) ++bleCount;
  scanLog.recordBle(record, source);
  syncBle();
  syncLog();
}

void pollBleBridge() {
#if USE_BLE_UART_BRIDGE
  BleAdvertisementRecord records[2];
  uint8_t count = bleBridge.readAvailable(records, 2, Serial);
  for (uint8_t i = 0; i < count; ++i) {
    ingestBleRecord(records[i], "uart-sidecar");
  }
  if (count > 0) eventLog.add("BLE bridge frame received");
#endif
}

// --- Serial commands ---
void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypherdrive-ops", storage.eventCount());
  Serial.print(F("[cypherdrive] flags "));
  Serial.println(flagState());
  Serial.print(F("[cypherdrive] scan_runs="));
  Serial.print(scanRuns);
  Serial.print(F(" wifi="));
  Serial.print(wifiCount);
  Serial.print(F(" ble="));
  Serial.println(bleCount);
  Serial.print(F("[cypherdrive] screen="));
  Serial.print(ui.screenName());
  Serial.print(F(" qr_mode="));
  Serial.println(qrStore.persistenceEnabled() ? F("preferences") : F("volatile"));
  Serial.println(F("[cypherdrive] safety=no join, no HID, no captive portal, no deauth, no capture"));
}

void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdScan(const String &args) {
  String mode = args;
  mode.trim();
  mode.toLowerCase();
  if (mode == "ble") {
    runBleScan();
    ui.showScreen("ble");
    return;
  }
  if (mode.length() == 0 || mode == "wifi") {
    runWifiScan();
    ui.showScreen("wifi");
    return;
  }
  Serial.println(F("[scan] usage: scan wifi | scan ble"));
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
    ingestBleRecord(record, "serial-smoke");
    eventLog.add("BLE bridge smoke frame parsed");
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
      syncLog();
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
  syncQr();
  ui.showScreen("qr");
}

void cmdLogs(const String &) {
  scanLog.print(Serial);
  eventLog.add("Scan log printed");
  syncLog();
}

void cmdScreen(const String &args) {
  String v = args;
  v.trim();
  if (v.length() > 0 && !ui.showScreen(v)) {
    Serial.println(F("[screen] usage: screen wifi|ble|log|qr"));
    return;
  }
  Serial.print(F("[screen] "));
  Serial.println(ui.screenName());
}

void cmdNet(const String &args) {
  String v = args;
  v.trim();
  if (v.startsWith("net ")) {
    v.remove(0, 4);
    v.trim();
  }
  int n = v.toInt();
  if (v.length() == 0 || n < 1 || n > wifiCount) {
    Serial.print(F("[net] usage: net <1.."));
    Serial.print(wifiCount);
    Serial.println(F("> - inspect a Wi-Fi row"));
    return;
  }
  uint8_t idx = (uint8_t)(n - 1);
  ui.selectNetwork(idx);
  const WifiNetworkRecord &r = wifiRows[idx];
  Serial.print(F("[net] #"));
  Serial.print(n);
  Serial.print(F(" ssid="));
  Serial.print(r.hidden ? "(hidden)" : r.ssid);
  Serial.print(F(" ch="));
  Serial.print(r.channel);
  Serial.print(F(" band="));
  Serial.print(r.channel <= 14 ? "2.4GHz" : "5GHz");
  Serial.print(F(" rssi="));
  Serial.print(r.rssi);
  Serial.print(F(" auth="));
  Serial.println(r.auth);
}

void cmdPage(const String &args) {
  String v = args;
  v.trim();
  v.toLowerCase();
  if (v == "prev" || v == "-") {
    ui.pageLog(-1);
  } else if (v == "next" || v == "+" || v.length() == 0) {
    ui.pageLog(+1);
  } else {
    int p = v.toInt();
    if (p < 1) {
      Serial.println(F("[page] usage: page next|prev|<n>"));
      return;
    }
    ui.setLogPage((uint8_t)(p - 1));
  }
  ui.showScreen("log");
  Serial.print(F("[page] "));
  Serial.print(ui.logPage() + 1);
  Serial.print(F("/"));
  Serial.println(ui.logPageCount());
}

void cmdTouch(const String &) { ui.printTouch(Serial); }

void cmdSelfTest(const String &) {
  uint16_t pass = 0, fail = 0;
  auto check = [&](const char *name, bool ok) {
    Serial.print(F("[selftest] "));
    Serial.print(ok ? F("PASS ") : F("FAIL "));
    Serial.println(name);
    if (ok) ++pass; else ++fail;
  };

  // 1. Wi-Fi scan path.
  WifiNetworkRecord w[WirelessOpsUi::kMaxWifi];
  uint8_t wc = wifiScanner.scan(w, WirelessOpsUi::kMaxWifi, Serial);
#if USE_WIFI_SCAN
  check("wifi scan path callable", true);
#else
  check("wifi mock scan returns rows", wc > 0);
  bool wifiFields = wc > 0 && w[0].ssid.length() > 0 && w[0].rssi < 0 && w[0].channel > 0;
  check("wifi rows carry ssid/rssi/channel", wifiFields);
#endif

  // 2. BLE scan path.
  BleAdvertisementRecord b[WirelessOpsUi::kMaxBle];
  uint8_t bc = bleBridge.scan(b, WirelessOpsUi::kMaxBle, Serial);
#if USE_BLE_UART_BRIDGE
  check("ble bridge scan path callable", true);
#else
  check("ble mock scan returns rows", bc > 0);
#endif

  // 3. Scan log records and reads back newest-first.
  ScanLog probe;
  if (wc > 0) probe.recordWifi(w[0], "selftest");
  if (bc > 0) probe.recordBle(b[0], "selftest");
  probe.recordInfo("selftest", "ran", "detail");
  ScanLog::Row row;
  bool got = probe.rowFromNewest(0, row);
  check("scan log newest readable", got && strcmp(row.category, "selftest") == 0);
  check("scan log counts info entry", probe.countType(ScanLog::kInfo) > 0);

  // 4. QR guard: rejects credential-like URLs, accepts clean ones.
  QrStateStore probeQr;
  probeQr.begin("selftest", CYPHERDRIVE_DEFAULT_QR_URL);
  bool rejected = !probeQr.setUrl("https://x.tld/?password=hunter2", Serial);
  check("qr rejects credential url", rejected);
  bool accepted = probeQr.setUrl("https://techtiff.ai/handoff", Serial);
  check("qr accepts clean url", accepted);

  // 5. UI navigation + inspector parity (headless-safe plain state).
  ui.setWifi(w, wc);
  check("ui nav to ble", ui.showScreen("ble") && ui.screen() == WSCR_BLE);
  check("ui nav to qr", ui.showScreen("qr") && ui.screen() == WSCR_QR);
  check("ui nav to log", ui.showScreen("log") && ui.screen() == WSCR_LOG);
  check("ui rejects unknown screen", !ui.showScreen("nope"));
  bool inspect = (wc > 0) ? (ui.selectNetwork(0) && ui.screen() == WSCR_WIFI && ui.detailOpen())
                          : ui.selectNetwork(0) == false;
  check("ui inspect wifi row", inspect);
  ui.closeDetail();

  // 6. Log paging wraps within bounds.
  ui.setLog(&scanLog);
  uint8_t pages = ui.logPageCount();
  ui.setLogPage(0);
  ui.pageLog(-1);
  check("ui log paging in bounds", ui.logPage() < pages);

  // 7. Passive banner is present on the chrome.
  check("passive banner present", strstr(ui.bannerText(), "PASSIVE") != nullptr);

  Serial.print(F("[selftest] SUMMARY pass="));
  Serial.print(pass);
  Serial.print(F(" fail="));
  Serial.println(fail);
  Serial.println(fail == 0 ? F("[selftest] RESULT PASS") : F("[selftest] RESULT FAIL"));
  eventLog.add(fail == 0 ? "Selftest PASS" : "Selftest FAIL");
  ui.setStatus(fail == 0 ? "selftest PASS" : "selftest FAIL");

  // Restore the live view.
  ui.showScreen("wifi");
  syncWifi();
  syncBle();
  syncLog();
  syncQr();
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CypherDrive Wireless Ops");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypherdrive");
  wifiScanner.begin();
  qrStore.begin("cypherdrive", CYPHERDRIVE_DEFAULT_QR_URL);

#if USE_BLE_UART_BRIDGE
  bleBridgeSerial.begin(CYPHERDRIVE_BLE_UART_BAUD, SERIAL_8N1, CYPHERDRIVE_BLE_UART_RX_PIN,
                        CYPHERDRIVE_BLE_UART_TX_PIN);
  if (CYPHERDRIVE_BLE_UART_RX_PIN < 0 || CYPHERDRIVE_BLE_UART_TX_PIN < 0) {
    Logger::warn("ble-bridge",
                 "UART pins unset; define CYPHERDRIVE_BLE_UART_RX_PIN/TX_PIN in config/Pins.h");
  }
  bleBridge.begin(&bleBridgeSerial);
#else
  bleBridge.begin(nullptr);
#endif

  ui.begin();
  ui.setLog(&scanLog);
  syncQr();

  // Mock-first: fill every screen with believable data before any input.
  runWifiScan();
  runBleScan();

  eventLog.add("CypherDrive Wireless Ops booted");
  scanLog.recordInfo("boot", "wireless ops ready", flagState());
  syncLog();

  router.begin(Serial, "cypherdrive");
  router.on("status", "uptime, heap, profile, flags, screen", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "scan wifi|ble - passive visibility scan", cmdScan);
  router.on("bridge", "bridge BLE,<label>,<addr>,<rssi>,<vendor>,<note>", cmdBridge);
  router.on("qr", "qr set <url> | qr show", cmdQr);
  router.on("logs", "print scan log state", cmdLogs);
  router.on("screen", "screen wifi|ble|log|qr", cmdScreen);
  router.on("net", "net <n> - inspect a Wi-Fi row", cmdNet);
  router.on("page", "page next|prev|<n> - page the scan log", cmdPage);
  router.on("touch", "raw/mapped touch coords + current screen", cmdTouch);
  router.on("selftest", "headless mock flow with PASS/FAIL summary", cmdSelfTest);
}

void loop() {
  router.poll();
  pollBleBridge();

  WirelessAction act = ui.tick();
  if (act == WACT_SCAN_WIFI) {
    runWifiScan();
  } else if (act == WACT_SCAN_BLE) {
    runBleScan();
  }

  delay(USE_DISPLAY ? 10 : 20);
}
