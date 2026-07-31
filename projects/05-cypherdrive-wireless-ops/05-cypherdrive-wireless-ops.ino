#include "config/ProjectConfig.h"
#include "src/BleC6.h"
#include "src/HidPad.h"
#include "src/PayloadRunner.h"
#include "src/ScanLog.h"
#include "src/SdStore.h"
#include "src/WifiOps.h"
#include "src/WirelessOpsUi.h"
#include <CrowPanelShared.h>

// Project 05 - CypherDrive Active Field Tool.
// A touch-first, four-screen console (WI-FI / BLE / HID / LOG) drawn with the
// shared Widgets toolkit, plus a full Serial command surface with 1:1 parity.
// ACTIVE by design: active Wi-Fi scan + join + client tools through the onboard
// C6, on-panel BLE central (scan/GATT) through the C6, and USB/BLE HID output
// via the shared CrowHid stack. Mock-first: every screen fills with believable
// data with no radio attached.
//
// NOT built (see ProjectConfig.h / TECHNICAL.md): Wi-Fi deauth/jamming, evil
// twin / captive-portal credential capture, or unattended BadUSB autorun.

WirelessOpsUi ui;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
WifiOps wifiOps;
BleC6 bleC6;
HidPad hidPad;
ScanLog scanLog;
SdStore sdStore;
PayloadRunner payloadRunner;
String sdPayloadNames[WirelessOpsUi::kMaxPayloads];
uint8_t sdPayloadCount = 0;

WifiNetworkRecord wifiRows[WirelessOpsUi::kMaxWifi];
uint8_t wifiCount = 0;
BleDeviceRecord bleRows[WirelessOpsUi::kMaxBle];
uint8_t bleCount = 0;
ServiceRecord svcRows[WirelessOpsUi::kMaxServices];
PortResult portRows[WirelessOpsUi::kMaxPorts];
BleServiceRecord bleSvcRows[WirelessOpsUi::kMaxBleServices];
uint16_t scanRuns = 0;
WifiLinkState lastLinkState = WLINK_IDLE;

String flagState() {
  return String("wifi_active=") + (USE_WIFI_ACTIVE ? "on" : "off") +
         " ble_c6=" + (USE_BLE_C6 ? "on" : "off") +
         " usb_hid=" + (USE_USB_HID ? "on" : "off") +
         " ble_hid=" + (USE_BLE_HID ? "on" : "off") +
         " sd=" + (USE_CYPHERDRIVE_SD ? "on" : "off") +
         " display=" + (USE_DISPLAY ? "on" : "off");
}

// --- UI sync helpers. The UI owns rendering only; the sketch pushes snapshots. ---
void syncWifi() { ui.setWifi(wifiRows, wifiCount); }
void syncBle() { ui.setBle(bleRows, bleCount); }
void syncLog() { ui.setLog(&scanLog); }
void syncLink() { ui.setLink(wifiOps.status()); }

// --- Scan flows (shared by touch and the `scan` serial command). ---
void runWifiScan() {
  ++scanRuns;
  const char *source = wifiOps.driverName();
  wifiCount = wifiOps.scan(wifiRows, WirelessOpsUi::kMaxWifi, Serial);
  if (wifiCount == 0) {
    scanLog.recordInfo("wifi", "no networks", String("source=") + source);
    eventLog.add("Wi-Fi scan returned no records");
  } else {
    for (uint8_t i = 0; i < wifiCount; ++i) scanLog.recordWifi(wifiRows[i], source);
    eventLog.add("Wi-Fi active scan complete");
  }
  syncWifi();
  syncLog();
  ui.setStatus(String("wifi: ") + wifiCount + " nets");
}

void runBleScan() {
  ++scanRuns;
  const char *source = bleC6.driverName();
  bleCount = bleC6.scan(bleRows, WirelessOpsUi::kMaxBle, Serial);
  ui.setBleServices(nullptr, 0, "");  // a fresh scan drops any GATT connection view
  if (bleCount == 0) {
    scanLog.recordInfo("ble", "no devices", String("source=") + source);
    eventLog.add("BLE scan returned no records");
  } else {
    for (uint8_t i = 0; i < bleCount; ++i) scanLog.recordBle(bleRows[i], source);
    eventLog.add("BLE central scan complete");
  }
  syncBle();
  syncLog();
  ui.setStatus(String("ble: ") + bleCount + " dev");
}

// --- Wi-Fi association + client tools ---
const char *joinPasswordFor(const WifiNetworkRecord &n) {
  if (n.open()) return nullptr;
  if (n.ssid == CYPHERDRIVE_JOIN_SSID && strlen(CYPHERDRIVE_JOIN_PASS) > 0) {
    return CYPHERDRIVE_JOIN_PASS;
  }
  return nullptr;  // secured but no configured key
}

void doJoin(uint8_t index) {
  if (index >= wifiCount) {
    Serial.println(F("[join] bad index"));
    return;
  }
  const WifiNetworkRecord &n = wifiRows[index];
  // A password typed on the on-screen keyboard (or via `pass`) wins; otherwise
  // fall back to the gitignored config key.
  String typed = ui.enteredPassword();
  const char *pass = typed.length() > 0 ? typed.c_str() : joinPasswordFor(n);
  if (!n.open() && (pass == nullptr || pass[0] == '\0')) {
    Serial.print(F("[join] no key for secured SSID '"));
    Serial.print(n.ssid);
    Serial.println(F("' - type one (tap JOIN) or add it to config/WiFiSecrets.h"));
  }
  wifiOps.join(n.ssid, pass, Serial);
  ui.clearEnteredPassword();
  scanLog.recordNet(String("join ") + n.ssid, String("auth=") + n.auth);
  eventLog.add("Wi-Fi join requested");
  syncLink();
  syncLog();
}

void doLeave() {
  wifiOps.leave(Serial);
  scanLog.recordNet("leave", "link down");
  syncLink();
  syncLog();
  ui.setCaptive(CAPTIVE_UNKNOWN);
}

void doCaptive() {
  ui.showBusy("captive check");
  CaptivePortalResult r = wifiOps.checkCaptivePortal(Serial);
  ui.setCaptive(r);
  const char *name = r == CAPTIVE_CLEAR ? "clear" : r == CAPTIVE_PORTAL ? "portal"
                     : r == CAPTIVE_OFFLINE ? "offline" : "unknown";
  scanLog.recordNet("captive-portal", name);
  eventLog.add("Captive-portal check");
  String lines[3];
  uint8_t li = 0;
  lines[li++] = String("Status: ") + name;
  if (r == CAPTIVE_CLEAR) lines[li++] = "Open internet - nothing intercepting (HTTP 204).";
  else if (r == CAPTIVE_PORTAL) lines[li++] = "A captive portal is intercepting requests.";
  else if (r == CAPTIVE_OFFLINE) lines[li++] = "No link - join a network first.";
  else lines[li++] = "Could not determine.";
  ui.showToolResult("Captive-portal detection", lines, li);
  syncLog();
}

void doMdns() {
  ui.showBusy("mDNS discovery");
  uint8_t n = wifiOps.discoverServices(svcRows, WirelessOpsUi::kMaxServices, Serial);
  ui.setServices(svcRows, n);
  scanLog.recordNet("mdns discovery", String(n) + " services");
  eventLog.add("mDNS discovery");
  String lines[WirelessOpsUi::kMaxToolLines];
  uint8_t li = 0;
  lines[li++] = String(n) + " service(s) found:";
  for (uint8_t i = 0; i < n && li < WirelessOpsUi::kMaxToolLines; ++i) {
    lines[li++] = svcRows[i].name + "  " + svcRows[i].type + "  " + svcRows[i].ip + ":" +
                  svcRows[i].port;
  }
  if (n == 0) {
    lines[li++] = wifiOps.status().state == WLINK_CONNECTED ? "None advertised on this LAN."
                                                            : "No link - join a network first.";
  }
  ui.showToolResult("mDNS / service discovery", lines, li);
  syncLog();
}

void doPortscan(const String &target) {
  ui.showBusy("port scan");
  uint8_t n = wifiOps.portScan(target, portRows, WirelessOpsUi::kMaxPorts, Serial);
  ui.setPorts(portRows, n);
  uint8_t open = 0;
  for (uint8_t i = 0; i < n; ++i) if (portRows[i].open) ++open;
  scanLog.recordNet("port scan", String(open) + " open / " + n + " probed");
  eventLog.add("Port scan");
  String lines[WirelessOpsUi::kMaxToolLines];
  uint8_t li = 0;
  lines[li++] = String(open) + " open of " + n + " probed (gateway):";
  for (uint8_t i = 0; i < n && li < WirelessOpsUi::kMaxToolLines; ++i) {
    lines[li++] = String(portRows[i].port) + "/" + portRows[i].service +
                  (portRows[i].open ? "  OPEN" : "  closed");
  }
  if (n == 0) lines[li++] = "No link - join a network first.";
  ui.showToolResult("Port scan", lines, li);
  syncLog();
}

void doSweep(uint16_t port) {
  ui.showBusy("host sweep");
  String live[24];
  uint8_t n = wifiOps.hostSweep(1, 24, port, live, 24, Serial);
  scanLog.recordNet("host sweep", String(n) + " live on :" + port);
  sdStore.appendRecon("sweep", String("port ") + port, String(n) + " live");
  for (uint8_t i = 0; i < n; ++i) sdStore.appendRecon("host", live[i], "up");
  eventLog.add("Host sweep");
  String lines[WirelessOpsUi::kMaxToolLines];
  uint8_t li = 0;
  lines[li++] = String(n) + " live host(s) on :" + port + " (gateway /24):";
  for (uint8_t i = 0; i < n && li < WirelessOpsUi::kMaxToolLines; ++i) lines[li++] = live[i];
  if (n == 0) {
    lines[li++] = wifiOps.status().state == WLINK_CONNECTED ? "None responded on that port."
                                                            : "No link - join a network first.";
  }
  ui.showToolResult("Host discovery (/24)", lines, li);
  syncLog();
}

// --- BLE central connect/enumerate ---
void doBleConnect(uint8_t index) {
  if (index >= bleCount) {
    Serial.println(F("[ble] bad index"));
    return;
  }
  if (bleC6.connect(bleRows[index], Serial)) {
    uint8_t n = bleC6.services(bleSvcRows, WirelessOpsUi::kMaxBleServices, Serial);
    ui.setBleServices(bleSvcRows, n, bleC6.connectedTo());
    scanLog.recordBle(bleRows[index], "gatt-connect");
    scanLog.recordNet("gatt enumerate", String(n) + " services");
  }
  eventLog.add("BLE connect");
  syncLog();
}

void doBleDisconnect() {
  bleC6.disconnect(Serial);
  ui.setBleServices(nullptr, 0, "");
  scanLog.recordNet("gatt disconnect", "");
  syncLog();
}

// --- SD export ---
void doSaveWifi(uint8_t index) {
  if (index >= wifiCount) { Serial.println(F("[save] bad index")); return; }
  bool ok = sdStore.appendWifi(wifiRows[index]);
  scanLog.recordNet("save wifi", wifiRows[index].ssid + (ok ? " -> SD" : " (log-only)"));
  ui.setStatus(ok ? "saved wifi -> SD" : "saved wifi (log-only)");
  syncLog();
}

void doSaveBle(uint8_t index) {
  if (index >= bleCount) { Serial.println(F("[save] bad index")); return; }
  bool ok = sdStore.appendBle(bleRows[index]);
  scanLog.recordNet("save ble", bleRows[index].address + (ok ? " -> SD" : " (log-only)"));
  ui.setStatus(ok ? "saved ble -> SD" : "saved ble (log-only)");
  syncLog();
}

// --- HID ---
void doHidSlot(uint8_t index) {
  hidPad.fireSlot(index);
  syncLog();
  ui.setStatus(String("hid: ") + hidPad.lastAction());
}

void doHidToggle() {
  hidPad.toggleOutput();
  syncLog();
  ui.setStatus(String("hid out: ") + hidPad.modeLabel());
}

uint16_t mediaUsageFor(const String &name) {
  if (name == "play" || name == "playpause") return kCcPlayPause;
  if (name == "mute") return kCcMute;
  if (name == "volup") return kCcVolumeUp;
  if (name == "voldown") return kCcVolumeDown;
  return 0;
}

// --- Payloads (presets + SD DuckyScript, played over the active HID output) ---
void rebuildPayloadList() {
  String names[WirelessOpsUi::kMaxPayloads];
  uint8_t n = 0;
  uint8_t pc = payloadRunner.presetCount();
  for (uint8_t i = 0; i < pc && n < WirelessOpsUi::kMaxPayloads; ++i) {
    names[n++] = payloadRunner.presetName(i);
  }
  sdPayloadCount = sdStore.listPayloads(sdPayloadNames,
                                        (uint8_t)(WirelessOpsUi::kMaxPayloads - n));
  for (uint8_t i = 0; i < sdPayloadCount && n < WirelessOpsUi::kMaxPayloads; ++i) {
    names[n++] = sdPayloadNames[i];
  }
  ui.setPayloads(names, n, pc);
}

void doRunPayload(uint8_t index) {
  uint8_t pc = payloadRunner.presetCount();
  if (index < pc) {
    payloadRunner.run(payloadRunner.presetName(index), payloadRunner.presetScript(index));
  } else {
    uint8_t sdi = (uint8_t)(index - pc);
    if (sdi >= sdPayloadCount) { Serial.println(F("[payload] bad index")); return; }
    String script;
    if (!sdStore.readPayload(sdPayloadNames[sdi], script)) {
      Serial.println(F("[payload] read failed (no card?)"));
      return;
    }
    payloadRunner.run(sdPayloadNames[sdi], script);
  }
  ui.showScreen("pld");
  ui.setStatus(String("payload: ") + payloadRunner.currentName());
  eventLog.add("Payload started");
  syncLog();
}

// --- UI event dispatch (touch actions execute the same flows as serial). ---
void handleUiEvent(const WirelessEvent &ev) {
  switch (ev.action) {
    case WACT_SCAN_WIFI: runWifiScan(); break;
    case WACT_JOIN_WIFI: doJoin((uint8_t)ev.index); break;
    case WACT_LEAVE_WIFI: doLeave(); break;
    case WACT_TOOL_CAPTIVE: doCaptive(); break;
    case WACT_TOOL_MDNS: doMdns(); break;
    case WACT_TOOL_PORTSCAN: doPortscan(CYPHERDRIVE_PORTSCAN_TARGET); break;
    case WACT_TOOL_SWEEP: doSweep(80); break;
    case WACT_SCAN_BLE: runBleScan(); break;
    case WACT_CONNECT_BLE: doBleConnect((uint8_t)ev.index); break;
    case WACT_DISCONNECT_BLE: doBleDisconnect(); break;
    case WACT_HID_SLOT: doHidSlot((uint8_t)ev.index); break;
    case WACT_HID_OUTPUT_TOGGLE: doHidToggle(); break;
    case WACT_SAVE_WIFI: doSaveWifi((uint8_t)ev.index); break;
    case WACT_SAVE_BLE: doSaveBle((uint8_t)ev.index); break;
    case WACT_RUN_PAYLOAD: doRunPayload((uint8_t)ev.index); break;
    case WACT_STOP_PAYLOAD: payloadRunner.stop(); syncLog(); break;
    default: break;
  }
}

// --- Serial commands ---
void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypherdrive-active", storage.eventCount(), &router);
  Serial.print(F("[cypherdrive] flags "));
  Serial.println(flagState());
  Serial.print(F("[cypherdrive] scan_runs="));
  Serial.print(scanRuns);
  Serial.print(F(" wifi="));
  Serial.print(wifiCount);
  Serial.print(F(" ble="));
  Serial.println(bleCount);
  Serial.print(F("[cypherdrive] link="));
  Serial.print(wifiOps.status().stateName());
  if (wifiOps.status().ip.length()) {
    Serial.print(F(" ip="));
    Serial.print(wifiOps.status().ip);
  }
  Serial.print(F(" hid_out="));
  Serial.println(hidPad.modeLabel());
  Serial.print(F("[cypherdrive] sd="));
  Serial.println(sdStore.statusLine());
  Serial.print(F("[cypherdrive] screen="));
  Serial.println(ui.screenName());
  Serial.println(F("[cypherdrive] excluded=no deauth, no evil-twin capture, no autorun HID"));
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

void cmdJoin(const String &args) {
  String v = args;
  v.trim();
  int n = v.toInt();
  if (v.length() == 0 || n < 1 || n > wifiCount) {
    Serial.print(F("[join] usage: join <1.."));
    Serial.print(wifiCount);
    Serial.println(F(">"));
    return;
  }
  ui.selectNetwork((uint8_t)(n - 1));
  doJoin((uint8_t)(n - 1));
}

void cmdPass(const String &args) {
  ui.setEnteredPassword(args);
  Serial.print(F("[pass] key set ("));
  Serial.print(args.length());
  Serial.println(F(" chars) - now run: join <n>"));
}

void cmdLeave(const String &) { doLeave(); }
void cmdCaptive(const String &) { doCaptive(); ui.showScreen("wifi"); }
void cmdMdns(const String &) { doMdns(); ui.showScreen("wifi"); }
void cmdPortscan(const String &args) {
  String t = args;
  t.trim();
  doPortscan(t.length() ? t : String(CYPHERDRIVE_PORTSCAN_TARGET));
  ui.showScreen("wifi");
}

void cmdDns(const String &args) {
  String h = args;
  h.trim();
  if (h.length() == 0) { Serial.println(F("[dns] usage: dns <hostname>")); return; }
  String ip;
  if (wifiOps.dnsLookup(h, ip, Serial)) {
    scanLog.recordNet("dns " + h, ip);
    sdStore.appendRecon("dns", h, ip);
  }
  syncLog();
}

void cmdBanner(const String &args) {
  String u = args;
  u.trim();
  if (u.length() == 0) { Serial.println(F("[banner] usage: banner <url>")); return; }
  String title, server;
  if (wifiOps.httpBanner(u, title, server, Serial)) {
    String d = "server=" + server + " title=" + title;
    scanLog.recordNet("banner " + u, d);
    sdStore.appendRecon("banner", u, d);
  }
  syncLog();
}

void cmdSweep(const String &args) {
  String v = args;
  v.trim();
  doSweep(v.length() ? (uint16_t)v.toInt() : 80);
}

void cmdOui(const String &args) {
  String m = args;
  m.trim();
  if (m.length() == 0) { Serial.println(F("[oui] usage: oui <mac>")); return; }
  String v = WifiOps::ouiVendor(m);
  Serial.print(F("[oui] "));
  Serial.print(m);
  Serial.print(F(" -> "));
  Serial.println(v.length() ? v : "(unknown)");
  scanLog.recordNet("oui " + m, v.length() ? v : "unknown");
  syncLog();
}

void cmdConnect(const String &args) {
  String v = args;
  v.trim();
  int n = v.toInt();
  if (v.length() == 0 || n < 1 || n > bleCount) {
    Serial.print(F("[connect] usage: connect <1.."));
    Serial.print(bleCount);
    Serial.println(F(">"));
    return;
  }
  ui.selectBle((uint8_t)(n - 1));
  doBleConnect((uint8_t)(n - 1));
  ui.showScreen("ble");
}

void cmdDisconnect(const String &) { doBleDisconnect(); }

void cmdHid(const String &args) {
  String v = args;
  v.trim();
  int n = v.toInt();
  if (v.length() == 0 || n < 1 || n > HidPad::kSlots) {
    Serial.print(F("[hid] usage: hid <1.."));
    Serial.print(HidPad::kSlots);
    Serial.println(F("> - fire a macro tile"));
    return;
  }
  doHidSlot((uint8_t)(n - 1));
  ui.showScreen("hid");
}

void cmdType(const String &args) {
  if (args.length() == 0) {
    Serial.println(F("[type] usage: type <text>"));
    return;
  }
  hidPad.typeText(args);
  syncLog();
  ui.showScreen("hid");
}

void cmdMedia(const String &args) {
  String v = args;
  v.trim();
  v.toLowerCase();
  uint16_t usage = mediaUsageFor(v);
  if (usage == 0) {
    Serial.println(F("[media] usage: media play|mute|volup|voldown"));
    return;
  }
  hidPad.media(usage);
  syncLog();
  ui.showScreen("hid");
}

void cmdOut(const String &) { doHidToggle(); ui.showScreen("hid"); }

void cmdSave(const String &args) {
  String v = args;
  v.trim();
  v.toLowerCase();
  if (v == "ble") {
    doSaveBle(ui.bleSelected());
  } else if (v.length() == 0 || v == "wifi") {
    doSaveWifi(ui.wifiSelected());
  } else {
    Serial.println(F("[save] usage: save wifi | save ble (saves the selected row)"));
    return;
  }
  Serial.print(F("[save] "));
  Serial.println(sdStore.statusLine());
}

void cmdPayload(const String &args) {
  String v = args;
  v.trim();
  String sub = v;
  int sp = v.indexOf(' ');
  String rest;
  if (sp >= 0) { sub = v.substring(0, sp); rest = v.substring(sp + 1); }
  sub.toLowerCase();
  if (sub == "stop") {
    payloadRunner.stop();
    syncLog();
    Serial.println(F("[payload] stopped"));
    return;
  }
  if (sub == "run") {
    int n = rest.toInt();
    uint8_t total = payloadRunner.presetCount() + sdPayloadCount;
    if (n < 1 || n > total) {
      Serial.print(F("[payload] usage: payload run <1.."));
      Serial.print(total);
      Serial.println(F(">"));
      return;
    }
    doRunPayload((uint8_t)(n - 1));
    return;
  }
  // list
  uint8_t pc = payloadRunner.presetCount();
  Serial.println(F("[payload] presets + SD (index for `payload run <n>`):"));
  for (uint8_t i = 0; i < pc; ++i) {
    Serial.print(F("  "));
    Serial.print(i + 1);
    Serial.print(F(". [preset] "));
    Serial.println(payloadRunner.presetName(i));
  }
  for (uint8_t i = 0; i < sdPayloadCount; ++i) {
    Serial.print(F("  "));
    Serial.print(pc + i + 1);
    Serial.print(F(". [SD] "));
    Serial.println(sdPayloadNames[i]);
  }
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
    Serial.println(F("[screen] usage: screen wifi|ble|hid|log"));
    return;
  }
  Serial.print(F("[screen] "));
  Serial.println(ui.screenName());
}

void cmdNet(const String &args) {
  String v = args;
  v.trim();
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
  Serial.print(F(" bssid="));
  Serial.print(r.bssid);
  Serial.print(F(" ch="));
  Serial.print(r.channel);
  Serial.print(F(" band="));
  Serial.print(r.band());
  Serial.print(F(" rssi="));
  Serial.print(r.rssi);
  Serial.print(F(" auth="));
  Serial.println(r.auth);
}

void cmdBle(const String &args) {
  String v = args;
  v.trim();
  int n = v.toInt();
  if (v.length() == 0 || n < 1 || n > bleCount) {
    Serial.print(F("[ble] usage: ble <1.."));
    Serial.print(bleCount);
    Serial.println(F("> - select a BLE row"));
    return;
  }
  ui.selectBle((uint8_t)(n - 1));
  const BleDeviceRecord &d = bleRows[n - 1];
  Serial.print(F("[ble] #"));
  Serial.print(n);
  Serial.print(F(" name="));
  Serial.print(d.name);
  Serial.print(F(" addr="));
  Serial.print(d.address);
  Serial.print(F(" rssi="));
  Serial.print(d.rssi);
  Serial.print(F(" connectable="));
  Serial.println(d.connectable ? "yes" : "no");
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

  // 1. Active Wi-Fi scan path.
  WifiNetworkRecord w[WirelessOpsUi::kMaxWifi];
  uint8_t wc = wifiOps.scan(w, WirelessOpsUi::kMaxWifi, Serial);
#if USE_WIFI_ACTIVE
  check("wifi active scan path callable", true);
#else
  check("wifi mock scan returns rows", wc > 0);
  bool wifiFields = wc > 0 && w[0].ssid.length() > 0 && w[0].rssi < 0 && w[0].channel > 0 &&
                    w[0].bssid.length() > 0;
  check("wifi rows carry ssid/bssid/rssi/channel", wifiFields);
#endif

  // 2. Join + link status.
  bool joined = wifiOps.join("SelftestNet", nullptr, Serial);
  wifiOps.maintain();
#if USE_WIFI_ACTIVE
  check("wifi join path callable", joined);
#else
  check("wifi mock join connects", joined && wifiOps.status().state == WLINK_CONNECTED);
#endif

  // 3. Client tools return without crashing (mock returns demo data).
  CaptivePortalResult cap = wifiOps.checkCaptivePortal(Serial);
  ServiceRecord sv[WirelessOpsUi::kMaxServices];
  uint8_t sc = wifiOps.discoverServices(sv, WirelessOpsUi::kMaxServices, Serial);
  PortResult pr[WirelessOpsUi::kMaxPorts];
  uint8_t pc = wifiOps.portScan("", pr, WirelessOpsUi::kMaxPorts, Serial);
#if USE_WIFI_ACTIVE
  check("client tools callable", true);
#else
  check("captive mock clear", cap == CAPTIVE_CLEAR);
  check("mdns mock returns services", sc > 0);
  check("portscan mock probes ports", pc > 0);
#endif

  // 4. BLE central scan + connect/enumerate.
  BleDeviceRecord b[WirelessOpsUi::kMaxBle];
  uint8_t bc = bleC6.scan(b, WirelessOpsUi::kMaxBle, Serial);
#if USE_BLE_C6
  check("ble central scan path callable", true);
#else
  check("ble mock scan returns rows", bc > 0);
  BleServiceRecord bs[WirelessOpsUi::kMaxBleServices];
  bool conn = bc > 0 && bleC6.connect(b[0], Serial);
  uint8_t bsc = conn ? bleC6.services(bs, WirelessOpsUi::kMaxBleServices, Serial) : 0;
  check("ble mock connect enumerates services", conn && bsc > 0);
  bleC6.disconnect(Serial);
#endif

  // 5. HID pad fires and logs.
  ScanLog probe;
  hidPad.fireSlot(0);
  if (wc > 0) probe.recordWifi(w[0], "selftest");
  if (bc > 0) probe.recordBle(b[0], "selftest");
  probe.recordNet("selftest", "net");
  probe.recordHid("selftest", "hid");
  ScanLog::Row row;
  bool got = probe.rowFromNewest(0, row);
  check("scan log newest readable", got && strcmp(row.category, "hid") == 0);
  check("scan log counts net entry", probe.countType(ScanLog::kNet) > 0);
  check("scan log counts hid entry", probe.countType(ScanLog::kHid) > 0);

  // 6. UI navigation across all four screens (headless-safe plain state).
  ui.setWifi(w, wc);
  ui.setBle(b, bc);
  check("ui nav to ble", ui.showScreen("ble") && ui.screen() == WSCR_BLE);
  check("ui nav to hid", ui.showScreen("hid") && ui.screen() == WSCR_HID);
  check("ui nav to log", ui.showScreen("log") && ui.screen() == WSCR_LOG);
  check("ui rejects unknown screen", !ui.showScreen("nope"));
  bool inspect = (wc > 0) ? (ui.selectNetwork(0) && ui.screen() == WSCR_WIFI && ui.detailOpen())
                          : ui.selectNetwork(0) == false;
  check("ui inspect wifi row", inspect);
  ui.closeDetail();

  // 7. Log paging wraps within bounds.
  ui.setLog(&scanLog);
  uint8_t pages = ui.logPageCount();
  ui.setLogPage(0);
  ui.pageLog(-1);
  check("ui log paging in bounds", ui.logPage() < pages);

  // 8. Active identity banner (no passive banner anymore).
  check("active banner present", strstr(ui.bannerText(), "ACTIVE") != nullptr);

  Serial.print(F("[selftest] SUMMARY pass="));
  Serial.print(pass);
  Serial.print(F(" fail="));
  Serial.println(fail);
  Serial.println(fail == 0 ? F("[selftest] RESULT PASS") : F("[selftest] RESULT FAIL"));
  eventLog.add(fail == 0 ? "Selftest PASS" : "Selftest FAIL");
  ui.setStatus(fail == 0 ? "selftest PASS" : "selftest FAIL");

  // Restore the live view.
  wifiOps.leave(Serial);
  ui.showScreen("wifi");
  runWifiScan();
  runBleScan();
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel CypherDrive Active Field Tool");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypherdrive");
  wifiOps.begin();
  bleC6.begin();
  hidPad.begin(&eventLog, &scanLog);
  sdStore.begin();
  payloadRunner.begin(&hidPad, &scanLog);

  ui.begin();
  ui.setHid(&hidPad);
  ui.setLog(&scanLog);
  rebuildPayloadList();
  syncLink();

  // Mock-first: fill every screen with believable data before any input.
  runWifiScan();
  runBleScan();

  eventLog.add("CypherDrive Active Field Tool booted");
  scanLog.recordInfo("boot", "active field tool ready", flagState());
  syncLog();

  router.begin(Serial, "cypherdrive");
  router.on("status", "uptime, heap, profile, flags, link, screen", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("scan", "scan wifi|ble - active scan", cmdScan);
  router.on("join", "join <n> - associate with a Wi-Fi row", cmdJoin);
  router.on("pass", "pass <key> - set the Wi-Fi key for the next join", cmdPass);
  router.on("leave", "drop the Wi-Fi association", cmdLeave);
  router.on("captive", "captive-portal detection on the link", cmdCaptive);
  router.on("mdns", "mDNS/service discovery on the LAN", cmdMdns);
  router.on("portscan", "portscan [target] - TCP connect sweep", cmdPortscan);
  router.on("dns", "dns <hostname> - resolve a host on the LAN/internet", cmdDns);
  router.on("banner", "banner <url> - HTTP title + Server header grab", cmdBanner);
  router.on("sweep", "sweep [port] - TCP host discovery on the gateway /24", cmdSweep);
  router.on("oui", "oui <mac> - vendor lookup from the MAC prefix", cmdOui);
  router.on("connect", "connect <n> - GATT connect a BLE row", cmdConnect);
  router.on("disconnect", "drop the BLE GATT connection", cmdDisconnect);
  router.on("hid", "hid <n> - fire a HID macro tile", cmdHid);
  router.on("type", "type <text> - send text over HID", cmdType);
  router.on("media", "media play|mute|volup|voldown", cmdMedia);
  router.on("out", "toggle HID output USB<->BLE", cmdOut);
  router.on("save", "save wifi|ble - export the selected row to SD", cmdSave);
  router.on("payload", "payload [list] | payload run <n> | payload stop", cmdPayload);
  router.on("logs", "print activity log state", cmdLogs);
  router.on("screen", "screen wifi|ble|hid|log", cmdScreen);
  router.on("net", "net <n> - inspect a Wi-Fi row", cmdNet);
  router.on("ble", "ble <n> - select a BLE row", cmdBle);
  router.on("page", "page next|prev|<n> - page the activity log", cmdPage);
  router.on("touch", "raw/mapped touch coords + current screen", cmdTouch);
  router.on("selftest", "headless mock flow with PASS/FAIL summary", cmdSelfTest);
}

void loop() {
  router.poll();

  wifiOps.maintain();
  if (wifiOps.status().state != lastLinkState) {
    lastLinkState = wifiOps.status().state;
    syncLink();
    scanLog.recordNet(String("link ") + wifiOps.status().stateName(),
                      wifiOps.status().ip.length() ? ("ip " + wifiOps.status().ip) : String(""));
    syncLog();
  }

  hidPad.service(millis());
  payloadRunner.service(millis());
  ui.setPayloadStatus(payloadRunner.currentName(), payloadRunner.progressPct(),
                      payloadRunner.running());

  WirelessEvent ev = ui.tick();
  if (ev.action != WACT_NONE) handleUiEvent(ev);

  delay(USE_DISPLAY ? 10 : 20);
}
