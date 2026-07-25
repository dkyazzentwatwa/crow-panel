#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/FlockBridgeClient.h"
#include "src/FlockC6Witness.h"
#include "src/FlockDashboard.h"
#include "src/FlockDetectionStore.h"
#include "src/FlockSessionStore.h"

// Project 16 - Cypher Flock Panel.
// The P4 is the touch UI/session host. A generic ESP32 DevKit performs native
// aggregates passive BLE from the ESP32 and dual-band Wi-Fi from a BW16.

SerialCommandRouter router;
FlockBridgeClient bridge;
FlockC6Witness c6Witness;
FlockDetectionStore detections;
FlockDetectionStore previousDetections;
FlockSessionStore sessions;
FlockDashboard dashboard;
FlockLifetimeStats lifetime;
HardwareSerial flockUart(FLOCK_UART_PORT);

String banner = "booting";
String storageStatus = "not initialized";
uint16_t lastSavedCount = 0;
uint32_t resetArmedUntil = 0;

void updateBanner(const String &message) {
  banner = message;
  dashboard.setBanner(message);
  Logger::info("flock", message);
}

void onBridgeEvent(const char *event, const char *message) {
  if (event && strcmp(event, "status") == 0 && (!message || !message[0])) {
    return;
  }
  String text = String(event && event[0] ? event : "event");
  if (message && message[0]) text += ": " + String(message);
  updateBanner(text);
}

void onCalibration(JsonObjectConst observation) {
  sessions.appendCalibration(observation, storageStatus);
}

void onDetection(const FlockDetection &d) {
  bool isNew = false;
  bool rediscovered = false;
  int16_t index = detections.upsert(d, isNew, rediscovered);
  if (index < 0) {
    updateBanner("detection table full; event dropped");
    return;
  }
  FlockDetection *stored = detections.at((uint16_t)index);
  if (stored) stored->rediscovered = rediscovered;
  if (d.alertEligible) {
    if (flockIsRaven(d)) ++lifetime.raven;
    else if (flockIsWifi(d)) ++lifetime.wifi;
    else ++lifetime.ble;
  }

  Serial.print(F("[flock:detection] type="));
  Serial.print(flockIsWifi(d) ? F("wifi") : (flockIsRaven(d) ? F("raven") : F("ble")));
  Serial.print(F(" mac="));
  Serial.print(d.mac);
  Serial.print(F(" rssi="));
  Serial.print(d.rssi);
  Serial.print(F(" confidence="));
  Serial.print(d.confidence);
  Serial.print(F(" state="));
  Serial.println(isNew ? F("new") : (rediscovered ? F("rediscovered") : F("updated")));

  updateBanner(String(isNew ? "new " : (rediscovered ? "rediscovered " : "updated ")) + d.mac);
  dashboard.requestRepaint();
}

void injectMock(const char *mac, const char *protocol, const char *capture,
                const char *method, const char *name, int rssi, int channel,
                int confidence, const char *extra = "") {
  JsonDocument doc;
  static uint32_t mockSequence = 1000000UL;
  doc["v"] = 1;
  doc["seq"] = ++mockSequence;
  doc["event"] = "detection";
  doc["mac_address"] = mac;
  doc["oui"] = String(mac).substring(0, 8);
  doc["protocol"] = protocol;
  doc["capture"] = capture;
  doc["detection_method"] = method;
  doc["device_name"] = name;
  doc["ssid"] = "";
  doc["extra"] = extra;
  doc["rssi"] = rssi;
  doc["channel"] = channel;
  doc["frequency"] = channel ? 2407 + channel * 5 : 0;
  doc["tx_power"] = 0;
  doc["confidence"] = confidence;
  doc["confidence_label"] = confidence >= 85 ? "CERTAIN" : (confidence >= 70 ? "HIGH" : "MEDIUM");
  doc["source"] = strstr(protocol, "wifi") ? "wifi-bw16" : "ble-esp32";
  doc["band"] = channel >= 32 ? "5" : (channel ? "2.4" : "ble");
  doc["catalog"] = "demo";
  doc["evidence"] = confidence >= 85 ? "high" : "medium";
  doc["signature_ids"] = "fixture.demo";
  doc["identity"] = mac;
  doc["candidate"] = false;
  doc["alert_eligible"] = true;
  doc["direct_rssi"] = true;
  String line;
  serializeJson(doc, line);
  bridge.inject(line);
}

void commandStatus(const String &) {
  const FlockBridgeStatus &s = bridge.status();
  Serial.printf("[status] uptime=%lus source=%s devices=%u previous=%u persistence=%s\n",
                (unsigned long)(millis() / 1000), bridge.driverName(), detections.count(),
                previousDetections.count(), sessions.ready() ? "ready" : "ram-only");
  Serial.printf("[status] link=%u scan=%s mode=%s channel=%u seq=%lu age_ms=%lu\n",
                (unsigned)s.link, s.scanState, s.mode, (unsigned)s.channel,
                (unsigned long)s.sequence,
                (unsigned long)(s.lastStatusMs ? millis() - s.lastStatusMs : 0));
  c6Witness.printStatus(Serial);
  Serial.printf("[status] wifi=%u ble=%u raven=%u lifetime=%lu/%lu/%lu parser=%lu oversized=%lu version=%lu dup=%lu order=%lu full=%lu\n",
                detections.protocolCount(kFlockFilterWifi), detections.protocolCount(kFlockFilterBle),
                detections.protocolCount(kFlockFilterRaven), (unsigned long)lifetime.wifi,
                (unsigned long)lifetime.ble, (unsigned long)lifetime.raven,
                (unsigned long)s.parserErrors, (unsigned long)s.oversizedLines,
                (unsigned long)s.unknownVersions, (unsigned long)s.duplicateLines,
                (unsigned long)s.outOfOrderLines, (unsigned long)detections.droppedFull());
}

void commandScreen(const String &args) {
  String value = args;
  value.toLowerCase();
  if (value == "scope") dashboard.setScreen(kFlockScopeScreen);
  else if (value == "feed") dashboard.setScreen(kFlockFeedScreen);
  else if (value == "witness" || value == "c6" || value == "wifi") dashboard.setScreen(kFlockWitnessScreen);
  else if (value == "stats") dashboard.setScreen(kFlockStatsScreen);
  else if (value == "control") dashboard.setScreen(kFlockControlScreen);
  else if (value == "next" || value.length() == 0) dashboard.nextScreen();
  else Serial.println(F("[screen] use scope|feed|witness|stats|control|next"));
}

void commandWitness(const String &args) {
  String value = args;
  value.toLowerCase();
  if (value == "scan" || value == "rescan") {
    c6Witness.requestScan();
    updateBanner("C6 passive witness scan requested");
    dashboard.requestRepaint();
  } else if (value == "list") {
    c6Witness.printNetworks(Serial);
  } else if (value == "screen") {
    dashboard.setScreen(kFlockWitnessScreen);
  } else {
    c6Witness.printStatus(Serial);
  }
}

void commandFilter(const String &args) {
  String value = args;
  value.toLowerCase();
  if (value == "wifi") dashboard.setFilter(kFlockFilterWifi);
  else if (value == "ble") dashboard.setFilter(kFlockFilterBle);
  else if (value == "raven") dashboard.setFilter(kFlockFilterRaven);
  else if (value == "candidate" || value == "candidates") dashboard.setFilter(kFlockFilterCandidate);
  else if (value == "bw16" || value == "wifi-bw16") dashboard.setFilter(kFlockFilterBw16);
  else if (value == "esp32" || value == "ble-esp32") dashboard.setFilter(kFlockFilterEsp32);
  else dashboard.setFilter(kFlockFilterAll);
}

void commandSource(const String &args) {
  String value = args;
  value.toLowerCase();
  dashboard.setPrevious(value == "previous" || value == "prev");
  Serial.println(dashboard.showingPrevious() ? F("[source] previous") : F("[source] current"));
}

void commandDemo(const String &) {
  injectMock("82:6b:f2:12:34:56", "wifi_2_4ghz", "FLOCK_WIFI", "wifi_wildcard_probe",
             "", -48, 6, 85);
  injectMock("44:38:39:aa:bb:cc", "bluetooth_le", "FLOCK_BLE", "ble_name ble_mac",
             "FS Ext Battery", -61, 0, 75);
  injectMock("10:20:30:40:50:60", "bluetooth_le", "RAVEN_BLE", "ble_raven_multi",
             "Raven", -55, 0, 95, "raven_fw=1.3.x uuid_count=5");
  updateBanner("demo injected: WiFi + BLE + Raven");
}

void commandInject(const String &args) {
  if (!bridge.inject(args)) Serial.println(F("[inject] rejected JSON (router max line is 95 chars)"));
}

void commandBridge(const String &args) {
  if (args.length() == 0) {
    Serial.println(F("[bridge] ping|status|diag on|off|profile precision|balanced|recall|band 2.4|5|dual|mode full|custom|single|channel <supported>|calibration on|off|export|clear|scan pause|resume|reset counters"));
    return;
  }
  if (!bridge.sendCommand(args)) Serial.println(F("[bridge] command rejected"));
}

void commandCalibration(const String &args) {
  String value = args;
  value.toLowerCase();
  if (value == "export") sessions.exportCalibration(Serial, storageStatus);
  else if (value == "clear") {
    sessions.clearCalibration(storageStatus);
    bridge.sendCommand("calibration clear");
  } else if (value == "on" || value == "off") bridge.sendCommand("calibration " + value);
  else storageStatus = "calibration on|off|export|clear";
  updateBanner(storageStatus);
}

void commandSave(const String &) {
  if (sessions.save(detections, lifetime, storageStatus)) lastSavedCount = detections.count();
  updateBanner(storageStatus);
}

void resetCurrentSession(bool sendBridgeReset) {
  detections.clear();
  sessions.clearCurrent(storageStatus);
  if (sendBridgeReset) bridge.sendCommand("reset counters");
  lastSavedCount = 0;
  dashboard.setPrevious(false);
  updateBanner(storageStatus);
}

void commandSession(const String &args) {
  String value = args;
  value.toLowerCase();
  if (value == "reset") resetCurrentSession(false);
  else if (value == "previous") dashboard.setPrevious(true);
  else if (value == "current") dashboard.setPrevious(false);
  else Serial.println(F("[session] reset|current|previous"));
}

void commandStealth(const String &args) {
  String value = args;
  value.toLowerCase();
  dashboard.setStealth(value == "on" || (value != "off" && !dashboard.stealth()));
  updateBanner(dashboard.stealth() ? "stealth on; tap display to wake" : "stealth off");
}

void commandSelftest(const String &) {
  uint32_t parserBefore = bridge.status().parserErrors;
  uint32_t versionBefore = bridge.status().unknownVersions;
  uint32_t oversizedBefore = bridge.status().oversizedLines;
  uint16_t countBefore = detections.count();
  commandDemo("");
  bool malformedRejected = !bridge.inject("{");
  bool versionRejected = !bridge.inject("{\"v\":2,\"event\":\"status\"}");
  String oversized;
  oversized.reserve(FLOCK_UART_MAX_LINE + 8);
  while (oversized.length() < FLOCK_UART_MAX_LINE) oversized += 'x';
  bool oversizedRejected = !bridge.inject(oversized);
  bool pass = malformedRejected && versionRejected && oversizedRejected &&
              bridge.status().parserErrors == parserBefore + 1 &&
              bridge.status().unknownVersions == versionBefore + 1 &&
              bridge.status().oversizedLines == oversizedBefore + 1 &&
              detections.count() >= 3 && detections.count() >= countBefore;
  Serial.printf("[selftest] status=%s malformed=%s version=%s oversized=%s detections=%u\n",
                pass ? "PASS" : "FAIL", malformedRejected ? "ok" : "fail",
                versionRejected ? "ok" : "fail", oversizedRejected ? "ok" : "fail",
                detections.count());
  updateBanner(pass ? "protocol selftest PASS" : "protocol selftest FAIL");
}

void handleUiAction(FlockUiAction action) {
  const FlockBridgeStatus &s = bridge.status();
  switch (action) {
    case kFlockUiPauseToggle:
      bridge.sendCommand(strcmp(s.scanState, "running") == 0 ? "scan pause" : "scan resume");
      break;
    case kFlockUiModeCycle:
      bridge.sendCommand(strcmp(s.mode, "full") == 0 ? "mode custom" :
                         (strcmp(s.mode, "custom") == 0 ? "mode single" : "mode full"));
      break;
    case kFlockUiBandCycle:
      bridge.sendCommand(strcmp(s.band, "dual") == 0 ? "band 2.4" :
                         (strcmp(s.band, "2.4") == 0 ? "band 5" : "band dual"));
      break;
    case kFlockUiProfileCycle:
      bridge.sendCommand(strcmp(s.profile, "precision") == 0 ? "profile balanced" :
                         (strcmp(s.profile, "balanced") == 0 ? "profile recall" : "profile precision"));
      break;
    case kFlockUiChannelNext:
      {
        static const uint8_t channels[] = {1, 6, 11, 36, 40, 44, 48, 149, 153, 157, 161, 165};
        uint8_t next = channels[0];
        for (uint8_t index = 0; index < sizeof(channels); ++index) {
          if (channels[index] == s.channel) { next = channels[(index + 1) % sizeof(channels)]; break; }
        }
        bridge.sendCommand("channel " + String(next));
      }
      break;
    case kFlockUiDiagnosticsToggle:
      bridge.sendCommand(s.diagnostics ? "diag off" : "diag on");
      break;
    case kFlockUiCalibrationToggle:
      bridge.sendCommand("calibration on");
      updateBanner("calibration capture enabled; use Serial to export or clear");
      break;
    case kFlockUiStealthToggle:
      dashboard.setStealth(!dashboard.stealth());
      break;
    case kFlockUiSave:
      commandSave("");
      break;
    case kFlockUiWitnessRefresh:
      c6Witness.requestScan();
      updateBanner("C6 passive witness scan requested");
      dashboard.requestRepaint();
      break;
    case kFlockUiReset:
      if (millis() < resetArmedUntil) {
        resetCurrentSession(false);
        resetArmedUntil = 0;
      } else {
        resetArmedUntil = millis() + 5000;
        updateBanner("reset armed; tap again within 5 seconds");
      }
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Logger::begin(115200);
  detections.clear();
  previousDetections.clear();

#if USE_FLOCK_UART_BRIDGE
  flockUart.begin(FLOCK_UART_BAUD, SERIAL_8N1, FLOCK_UART_RX_PIN, FLOCK_UART_TX_PIN);
  bridge.begin(&flockUart, onDetection, onBridgeEvent, onCalibration);
#else
  bridge.begin(nullptr, onDetection, onBridgeEvent, onCalibration);
#endif

  sessions.begin(previousDetections, lifetime, storageStatus);
  c6Witness.begin();
  dashboard.begin();
  dashboard.setBanner(storageStatus);

  router.begin(Serial, "cypher-flock-panel");
  router.on("status", "show panel, bridge, detector, and storage state", commandStatus);
  router.on("screen", "scope|feed|witness|stats|control|next", commandScreen);
  router.on("witness", "status|scan|list|screen", commandWitness);
  router.on("filter", "all|wifi|ble|raven|candidate|bw16|esp32", commandFilter);
  router.on("source", "current|previous", commandSource);
  router.on("demo", "inject deterministic WiFi, BLE, and Raven hits", commandDemo);
  router.on("inject", "inject a compact v1 detection JSON object", commandInject);
  router.on("bridge", "send a scanner command to the ESP32 bridge", commandBridge);
  router.on("calibration", "on|off|export|clear sanitized observations", commandCalibration);
  router.on("save", "atomically save current session to FFat", commandSave);
  router.on("session", "reset|current|previous", commandSession);
  router.on("stealth", "on|off", commandStealth);
  router.on("selftest", "exercise detection and invalid-frame parser paths", commandSelftest);

  updateBanner(String("ready source=") + bridge.driverName());
  commandStatus("");
}

void loop() {
  router.poll();
  bridge.tick();
  c6Witness.tick();
  detections.tickActivity();

  if (sessions.ready() && detections.count() != lastSavedCount &&
      millis() - sessions.lastSaveMs() >= 60000UL) {
    if (sessions.save(detections, lifetime, storageStatus)) lastSavedCount = detections.count();
  }

  FlockUiEvent event;
  if (dashboard.tick(detections, previousDetections, bridge.status(), lifetime, c6Witness,
                     sessions.ready(), event)) {
    handleUiAction(event.action);
  }
  delay(2);
}
