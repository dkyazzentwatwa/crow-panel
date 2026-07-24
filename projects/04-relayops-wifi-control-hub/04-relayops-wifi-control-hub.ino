#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/HubTypes.h"
#include "src/DeviceController.h"
#include "src/SensorServer.h"
#include "src/MockSensorSource.h"
#include "src/ControlHubUi.h"

// RelayOps is a Wi-Fi hub, not a radio node: it RUNS a web server so remote
// ESP32s can POST sensor data in (SensorServer), and it SENDS HTTP GPIO
// commands out to toggle their lights/relays (DeviceController). Same
// FieldOps-style dashboard, no LoRa/ESP-NOW. Default build (USE_WIFI=0) is
// fully offline: MockSensorSource feeds the roster and `set` drives devices
// with log-only commands.
ControlHubUi ui;
DeviceController devices;
SensorServer server;
EventLog eventLog;
StorageManager storage;
CrowNetworkClient network;
SerialCommandRouter router;
WorldFeedClient world;
WorldFeeds worldFeeds;
#if !USE_WIFI
MockSensorSource mockSource;
#endif

// --- Small parsing helpers (defined before use: the ctags workaround skips
// prototype generation, so order matters). ---

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

// Returns the token before the next comma (from `pos`) and advances `pos`.
String nextField(const String &line, int &pos) {
  int comma = line.indexOf(',', pos);
  String field = (comma < 0) ? line.substring(pos) : line.substring(pos, comma);
  pos = (comma < 0) ? line.length() : comma + 1;
  field.trim();
  return field;
}

// Parse a bench CSV frame (same shape as FieldOps' `feed`) into a reading:
//   SENSOR,<name>,<tempC>,<hum>,<batt>,<motion0/1>,<rssi>
//   PRESENCE,<name>,<rssi>,<type>
bool parseSensorCsv(const String &line, SensorReading &out) {
  String trimmed = line;
  trimmed.trim();
  if (trimmed.length() == 0) return false;

  int pos = 0;
  String kind = nextField(trimmed, pos);

  if (kind == "SENSOR") {
    out.nodeId = nextField(trimmed, pos);
    if (out.nodeId.length() == 0) return false;
    out.temperatureC = nextField(trimmed, pos).toFloat();
    out.humidityPct = nextField(trimmed, pos).toFloat();
    out.batteryPct = nextField(trimmed, pos).toFloat();
    out.motion = nextField(trimmed, pos).toInt() != 0;
    out.rssi = nextField(trimmed, pos).toInt();
    out.presenceOnly = false;
    out.receivedAtMs = millis();
    return true;
  }

  if (kind == "PRESENCE") {
    out.nodeId = nextField(trimmed, pos);
    if (out.nodeId.length() == 0) return false;
    out.rssi = nextField(trimmed, pos).toInt();
    out.temperatureC = NAN;
    out.humidityPct = NAN;
    out.batteryPct = NAN;
    out.motion = false;
    out.presenceOnly = true;
    out.receivedAtMs = millis();
    return true;
  }

  return false;
}

// Seed one device from the static RELAYOPS_STATIC_DEVICES table.
void seedDevice(const char *id, const char *host, const char *path, uint8_t pin) {
  ControlDevice dev;
  dev.deviceId = id;
  dev.host = host;
  dev.path = path;
  dev.pin = pin;
  devices.addDevice(dev);
}

// --- Inbound pipeline: every reading (mock, `feed`, or a real POST /sensor)
// funnels through here. ---
void onSensor(const SensorReading &reading) {
  ui.renderSensor(reading);
  if (reading.presenceOnly) {
    ui.renderEvent(String(reading.nodeId) + " checked in");
    return;
  }
  storage.incrementEventCount();
  eventLog.add(String("sensor ") + reading.nodeId);
  // Mock JSON, unescaped - swap for real serialization (ArduinoJson) before a
  // backend ingests this for real. Mirrors the hub's data onto the event log.
  network.postEvent(String("{\"source\":\"relayops\",\"node\":\"") + reading.nodeId + "\"}");
  ui.renderEvent(String(reading.nodeId) + " reported in");
}

// A node self-registered a controllable pin (POST /register or a control_url
// on POST /sensor).
void onRegister(const ControlDevice &device) {
  ControlDevice *stored = devices.registerDevice(device.deviceId, device.host, device.path, device.pin);
  if (stored != nullptr) ui.renderDevice(*stored);
  eventLog.add(String("register ") + device.deviceId);
  ui.renderEvent(String("registered ") + device.deviceId);
}

// --- Outbound: command a device and reflect the new state on the dashboard.
void applyDeviceState(const String &id, bool on) {
  bool ok = devices.setPin(id, on);
  ControlDevice *dev = devices.find(id);
  // Reflect the (possibly unchanged, on failure) state back onto the dashboard
  // so the detail screen's "last command / result" trail resolves either way.
  if (dev != nullptr) ui.renderDevice(*dev);
  if (!ok) {
    ui.renderEvent(id + " command failed");
    return;
  }
  eventLog.add(id + (on ? " ON" : " OFF"));
  ui.renderEvent(id + (on ? " -> ON" : " -> OFF"));
}

// --- World feeds: pull the latest snapshot and paint the World screen. Shared
// by the `world` command and the on-screen REFRESH button.
void refreshWorld(const String &which) {
  String w = which;
  w.trim();
  if (w.length() == 0) w = "all";
  if (world.refresh(worldFeeds, w)) {
    ui.renderWorld(worldFeeds);
  }
}

// --- Serial commands ---
void cmdStatus(const String &) {
  printSystemStatus(Serial, "relayops", storage.eventCount());
}

void cmdDevices(const String &) {
  Serial.println(F("[devices] id / state / gpio / target"));
  for (uint8_t i = 0; i < devices.count(); i++) {
    const ControlDevice &d = devices.at(i);
    Serial.printf("  %-14s %-3s GPIO %-3u http://%s%s\n", d.deviceId.c_str(),
                  d.state ? "ON" : "OFF", (unsigned)d.pin, d.host.c_str(), d.path.c_str());
  }
}

void cmdSet(const String &args) {
  String rest = args;
  String id = nextWord(rest);
  String stateWord = nextWord(rest);
  if (id.length() == 0) {
    Logger::warn("cmd", "usage: set <deviceId> <on|off|toggle>");
    return;
  }
  ControlDevice *dev = devices.find(id);
  if (dev == nullptr) {
    Logger::warn("cmd", "unknown device " + id + " (try `devices`)");
    return;
  }
  stateWord.toLowerCase();
  bool on;
  if (stateWord == "on") {
    on = true;
  } else if (stateWord == "off") {
    on = false;
  } else if (stateWord.length() == 0 || stateWord == "toggle") {
    on = !dev->state;
  } else {
    Logger::warn("cmd", "state must be on|off|toggle");
    return;
  }
  applyDeviceState(id, on);
}

void cmdFeed(const String &args) {
  SensorReading reading;
  if (parseSensorCsv(args, reading)) {
    Logger::info("cmd", "feed " + reading.nodeId);
    onSensor(reading);
  } else {
    Logger::warn("cmd",
                 "bad frame; use: feed SENSOR,name,tempC,hum,batt,motion,rssi  or  feed PRESENCE,name,rssi,type");
  }
}

void cmdWorld(const String &args) {
  String which = args;
  which.trim();
  if (which.length() == 0) which = "all";
  Logger::info("cmd", "world refresh " + which);
  refreshWorld(which);
}

void cmdTouch(const String &) {
  ui.printTouch(Serial);
}

void cmdScreen(const String &args) {
  String name = args;
  name.trim();
  if (name.length() == 0) {
    Serial.print(F("[screen] current="));
    Serial.println(ui.screenName());
    Serial.println(F("[screen] usage: screen <devices|detail|sensors|world|events>"));
    return;
  }
  if (ui.selectScreen(name)) {
    Serial.print(F("[screen] -> "));
    Serial.println(ui.screenName());
  } else {
    Logger::warn("cmd", "unknown screen " + name +
                            " (devices|detail|sensors|world|events)");
  }
}

void cmdSensor(const String &args) {
  String name = args;
  name.trim();
  if (name.length() == 0) {
    Serial.println(F("[sensor] usage: sensor <nodeName> - pin a node into the gauges"));
    Serial.print(F("[sensor] on screen "));
    Serial.println(ui.screenName());
    return;
  }
  if (ui.selectSensor(name)) {
    Serial.print(F("[sensor] pinned "));
    Serial.println(name);
  } else {
    Logger::warn("cmd", "no live sensor '" + name +
                            "' to pin (display build only; feed one first)");
  }
}

// End-to-end mock flow check: drives the real app objects (DeviceController,
// the sensor pipeline, and the world feeds) headlessly and prints PASS/FAIL
// lines. Works with no panel attached - it exercises logic, not the display.
void cmdSelfTest(const String &) {
  uint8_t pass = 0, fail = 0;
  auto check = [&](const char *name, bool ok) {
    char line[80];
    snprintf(line, sizeof(line), "[selftest] %-38s %s", name, ok ? "PASS" : "FAIL");
    Serial.println(line);
    if (ok) pass++; else fail++;
  };

  // 1) A device is seeded and controllable through the same path touch uses.
  bool haveDevice = devices.count() > 0;
  check("device registry seeded", haveDevice);

  if (haveDevice) {
    const String id = devices.at(0).deviceId;
    applyDeviceState(id, true);
    ControlDevice *d = devices.find(id);
    check("device ON commanded", d != nullptr && d->state);
    applyDeviceState(id, false);
    d = devices.find(id);
    check("device OFF commanded", d != nullptr && !d->state);
    bool before = d != nullptr && d->state;
    applyDeviceState(id, !before);  // toggle
    d = devices.find(id);
    check("device TOGGLE flips state", d != nullptr && d->state != before);
  } else {
    check("device ON commanded", false);
    check("device OFF commanded", false);
    check("device TOGGLE flips state", false);
  }

  // 2) A telemetry frame parses and drives the inbound pipeline.
  uint32_t eventsBefore = storage.eventCount();
  SensorReading reading;
  bool parsed = parseSensorCsv("SENSOR,SELFTEST,24.5,44,90,1,-61", reading);
  check("telemetry CSV parses", parsed && reading.nodeId == "SELFTEST");
  if (parsed) onSensor(reading);
  check("telemetry increments event count", storage.eventCount() == eventsBefore + 1);

  // 3) A presence frame parses and is flagged presence-only.
  SensorReading presence;
  bool presParsed = parseSensorCsv("PRESENCE,SELFTEST-P,-70,heartbeat", presence);
  check("presence CSV parses", presParsed && presence.presenceOnly);
  if (presParsed) onSensor(presence);

  // 4) A malformed frame is rejected.
  SensorReading bad;
  check("malformed frame rejected", !parseSensorCsv("GARBAGE,only", bad));

  // 5) The world feeds populate (canned in mock mode, live under USE_WIFI).
  refreshWorld("all");
  check("world weather valid", worldFeeds.weatherValid);
  check("world aurora valid", worldFeeds.auroraValid);

  // 6) Screen navigation parity for every tabbed screen.
  bool nav = ui.selectScreen("sensors") && ui.selectScreen("world") &&
             ui.selectScreen("events") && ui.selectScreen("devices");
  check("screen navigation reachable", nav);

  char summary[72];
  snprintf(summary, sizeof(summary), "[selftest] overall %s  (%u pass, %u fail)",
           fail == 0 ? "PASS" : "FAIL", pass, fail);
  Serial.println(summary);
  eventLog.add(fail == 0 ? "Selftest PASS" : "Selftest FAIL");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel RelayOps WiFi Control Hub");

  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);

  storage.begin("relayops");
  network.begin(RELAYOPS_API_ENDPOINT, WIFI_SSID, WIFI_PASS);

  // Seed the static controllable-device table (config/Devices.h or defaults).
#define RELAYOPS_DEVICE(id, host, path, pin) seedDevice(id, host, path, (uint8_t)(pin));
  RELAYOPS_STATIC_DEVICES
#undef RELAYOPS_DEVICE

  server.begin(RELAYOPS_SERVER_PORT, onSensor, onRegister);
  ui.begin();

  // Paint the seeded devices onto the roster.
  for (uint8_t i = 0; i < devices.count(); i++) {
    ui.renderDevice(devices.at(i));
  }

  world.begin(RELAYOPS_LAT, RELAYOPS_LON, RELAYOPS_PLACE, RELAYOPS_KP_THRESHOLD);

#if !USE_WIFI
  mockSource.begin();
#endif

  eventLog.add("RelayOps hub booted");

  router.begin(Serial, "relayops");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("devices", "list controllable devices and their targets", cmdDevices);
  router.on("set", "set <deviceId> <on|off|toggle> - command a device's GPIO", cmdSet);
  router.on("feed", "inject a sensor frame, e.g. feed SENSOR,ATTIC,29.5,40,88,0,-58", cmdFeed);
  router.on("world", "print/refresh weather, quakes, aurora, air", cmdWorld);
  router.on("screen", "switch/report UI screen: screen <devices|detail|sensors|world|events>", cmdScreen);
  router.on("sensor", "pin a node into the gauges: sensor <nodeName> (serial parity for the sensor tap)", cmdSensor);
  router.on("touch", "print raw + mapped touch coords, tap count, current screen", cmdTouch);
  router.on("selftest", "drive the mock flow end-to-end and print PASS/FAIL", cmdSelfTest);
}

void loop() {
  router.poll();
  network.maintain();
  server.handle();

  // Execute the typed UI event the dashboard produced this frame. The UI never
  // mutates app state itself - it only asks, and the sketch acts.
  HubUiEvent ev = ui.tick();
  switch (ev.type) {
    case kHubSetDevice:
      applyDeviceState(ev.deviceId, ev.on);
      break;
    case kHubRefreshWorld:
      refreshWorld("all");
      break;
    case kHubNone:
    default:
      break;
  }

#if !USE_WIFI
  // No web server in mock mode: synthesize readings so the roster stays live.
  SensorReading reading;
  if (mockSource.poll(reading)) {
    onSensor(reading);
  }
#endif

  if (world.poll(worldFeeds)) {
    ui.renderWorld(worldFeeds);
  }

  // Small yield only; demo cadence comes from Throttle gates.
  delay(20);
}
