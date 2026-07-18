// Cypher Flock BLE scanner and three-board protocol aggregator.
// Wi-Fi is intentionally disabled. The BW16 owns all 802.11 capture.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FlockSignatureCatalog.h>
#include <NimBLEDevice.h>
#include <WiFi.h>

#define PANEL_RX_PIN 16
#define PANEL_TX_PIN 17
#define BW16_RX_PIN 32
#define BW16_TX_PIN 33
#define LINK_BAUD 115200
#define DETECTION_LED_PIN 27
#define MAX_PANEL_LINE 192
#define MAX_BW16_LINE 768
#define BLE_QUEUE_SIZE 16
#define STATUS_INTERVAL_MS 2000UL
#define DIAG_INTERVAL_MS 10000UL
#define STALE_MS 5000UL
#define OFFLINE_MS 10000UL
#define EMIT_COOLDOWN_MS 5000UL
#define REDISCOVER_MS 300000UL

HardwareSerial PanelLink(1);
HardwareSerial Bw16Link(2);

enum ProfileMode : uint8_t { kPrecision = 0, kBalanced, kRecall };

struct BleDetection {
  char mac[18];
  char oui[9];
  char name[33];
  char identity[33];
  char method[64];
  char signatureIds[96];
  char extra[64];
  int8_t rssi;
  int16_t txPower;
  uint8_t confidence;
  uint8_t ravenCustom;
  uint8_t ravenContext;
  FlockCatalog::EvidenceTier evidence;
  bool candidate;
  bool alertEligible;
  bool raven;
};

struct RecentEmission {
  char identity[34];
  uint32_t lastSeen;
  uint32_t lastEmit;
};

struct Bw16Status {
  char radioPhase[20];
  char band[8];
  char scanMode[10];
  uint32_t framesMgmt;
  uint32_t framesData;
  uint32_t queueDrops;
  uint32_t parserErrors;
  uint32_t oversizedLines;
  uint32_t channelErrors;
  uint32_t targetCycles;
  uint32_t fullSweeps;
  uint32_t listen24Ms;
  uint32_t listen5Ms;
  uint8_t channel;
  bool fallback;
};

static volatile BleDetection bleQueue[BLE_QUEUE_SIZE];
static volatile uint8_t bleHead = 0;
static volatile uint8_t bleTail = 0;
static portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;
static RecentEmission recent[24] = {};
static uint8_t recentNext = 0;
static Bw16Status bwStatus = {};
static NimBLEScan *bleScan = nullptr;
static ProfileMode profileMode = kBalanced;
static uint32_t globalSequence = 0;
static uint32_t lastBw16Sequence = 0;
static uint32_t lastBw16RxMs = 0;
static uint32_t lastBleEngineMs = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastDiagMs = 0;
static uint32_t ledOffMs = 0;
static uint32_t bleReports = 0;
static uint32_t bleScanStarts = 0;
static uint32_t emittedBle = 0;
static uint32_t emittedRaven = 0;
static uint32_t bleQueueDrops = 0;
static uint32_t panelParserErrors = 0;
static uint32_t panelOversized = 0;
static uint32_t bwParserErrors = 0;
static uint32_t bwOversized = 0;
static uint32_t bwDuplicate = 0;
static uint32_t bwOutOfOrder = 0;
static bool scanPaused = false;
static bool diagnosticsEnabled = false;
static bool calibrationEnabled = false;
static char panelLine[MAX_PANEL_LINE] = {};
static size_t panelLineLen = 0;
static bool panelDiscarding = false;
static char usbLine[MAX_PANEL_LINE] = {};
static size_t usbLineLen = 0;
static bool usbDiscarding = false;
static char bwLine[MAX_BW16_LINE] = {};
static size_t bwLineLen = 0;
static bool bwDiscarding = false;

static const char *profileName() {
  if (profileMode == kPrecision) return "precision";
  if (profileMode == kRecall) return "recall";
  return "balanced";
}

static const char *healthFor(uint32_t lastSeen) {
  if (lastSeen == 0 || millis() - lastSeen >= OFFLINE_MS) return "offline";
  if (millis() - lastSeen >= STALE_MS) return "stale";
  return "online";
}

static const char *evidenceName(FlockCatalog::EvidenceTier tier) {
  if (tier == FlockCatalog::kHigh) return "high";
  if (tier == FlockCatalog::kMedium) return "medium";
  return "candidate";
}

static bool alertEligible(FlockCatalog::EvidenceTier tier) {
  if (tier == FlockCatalog::kHigh) return true;
  if (tier == FlockCatalog::kMedium) return profileMode != kPrecision;
  return profileMode == kRecall;
}

static void sendPanel(JsonDocument &doc, const char *source) {
  doc["v"] = 1;
  doc["seq"] = ++globalSequence;
  doc["source"] = source;
  serializeJson(doc, PanelLink);
  PanelLink.write('\n');
}

static void sendAck(const char *command, bool ok, const String &message) {
  JsonDocument doc;
  doc["event"] = ok ? "ack" : "error";
  doc["command"] = command;
  doc["ok"] = ok;
  doc["message"] = message;
  sendPanel(doc, "ble-esp32");
}

static bool parseMac(const char *text, uint8_t *mac) {
  unsigned values[6];
  if (!text || sscanf(text, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2],
                      &values[3], &values[4], &values[5]) != 6) return false;
  for (uint8_t index = 0; index < 6; ++index) mac[index] = (uint8_t)values[index];
  return true;
}

static void ouiFromMac(const uint8_t *mac, char *out, size_t capacity) {
  snprintf(out, capacity, "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
}

static void appendText(char *target, size_t capacity, const char *text) {
  size_t used = strlen(target);
  if (!text || used >= capacity - 1) return;
  strncat(target, text, capacity - used - 1);
}

static void addSignature(char *target, size_t capacity, const char *id) {
  if (!id || !id[0]) return;
  if (target[0]) appendText(target, capacity, ",");
  appendText(target, capacity, id);
}

static bool equalsIgnoreCase(const char *left, const char *right) {
  return left && right && strcasecmp(left, right) == 0;
}

static bool startsIgnoreCase(const char *value, const char *prefix) {
  return value && prefix && strncasecmp(value, prefix, strlen(prefix)) == 0;
}

static bool exactPenguin(const char *name) {
  if (!name || strlen(name) != 18 || strncmp(name, "Penguin-", 8) != 0) return false;
  for (size_t index = 8; index < 18; ++index) if (!isdigit((unsigned char)name[index])) return false;
  return true;
}

static bool exactFlock(const char *name) {
  if (!name || strlen(name) != 12 || strncmp(name, "Flock-", 6) != 0) return false;
  for (size_t index = 6; index < 12; ++index) {
    if (!isxdigit((unsigned char)name[index]) || islower((unsigned char)name[index])) return false;
  }
  return true;
}

static bool flockSuffixMatches(const char *name, const uint8_t *mac) {
  if (!exactFlock(name)) return false;
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return strcmp(name + 6, suffix) == 0;
}

static const FlockCatalog::OuiSignature *findOui(const uint8_t *mac) {
  if ((mac[0] & 0x01) != 0) return nullptr;
  for (size_t index = 0; index < FlockCatalog::WIFI_OUI_COUNT; ++index) {
    const FlockCatalog::OuiSignature &item = FlockCatalog::WIFI_OUIS[index];
    if (mac[0] == item.bytes[0] && mac[1] == item.bytes[1] && mac[2] == item.bytes[2]) return &item;
  }
  return nullptr;
}

static bool hasTnSerial(const std::string &data) {
  for (size_t index = 0; index + 1 < data.length(); ++index) {
    if (data[index] == 'T' && data[index + 1] == 'N') return true;
  }
  return false;
}

static int countUuidMatches(const NimBLEAdvertisedDevice *device,
                            const FlockCatalog::UuidSignature *items, size_t itemCount) {
  if (!device || !device->haveServiceUUID()) return 0;
  int matched = 0;
  for (int uuidIndex = 0; uuidIndex < device->getServiceUUIDCount(); ++uuidIndex) {
    std::string uuid = device->getServiceUUID(uuidIndex).toString();
    for (size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
      if (strcasecmp(uuid.c_str(), items[itemIndex].value) == 0) { ++matched; break; }
    }
  }
  return matched;
}

static void queueBle(const BleDetection &detection) {
  portENTER_CRITICAL(&bleMux);
  uint8_t next = (uint8_t)((bleHead + 1) % BLE_QUEUE_SIZE);
  if (next == bleTail) ++bleQueueDrops;
  else { memcpy((void *)&bleQueue[bleHead], &detection, sizeof(detection)); bleHead = next; }
  portEXIT_CRITICAL(&bleMux);
}

class AggregatorBleCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *device) override {
    ++bleReports;
    lastBleEngineMs = millis();
    BleDetection hit = {};
    std::string address = device->getAddress().toString();
    uint8_t mac[6] = {};
    if (!parseMac(address.c_str(), mac)) return;
    strlcpy(hit.mac, address.c_str(), sizeof(hit.mac));
    ouiFromMac(mac, hit.oui, sizeof(hit.oui));
    if (device->haveName()) strlcpy(hit.name, device->getName().c_str(), sizeof(hit.name));
    hit.rssi = device->getRSSI();
    hit.txPower = device->haveTXPower() ? device->getTXPower() : 0;
    hit.ravenCustom = countUuidMatches(device, FlockCatalog::RAVEN_CUSTOM_UUIDS,
                                       FlockCatalog::RAVEN_CUSTOM_UUID_COUNT);
    hit.ravenContext = countUuidMatches(device, FlockCatalog::RAVEN_CONTEXT_UUIDS,
                                        FlockCatalog::RAVEN_CONTEXT_UUID_COUNT);
    hit.raven = hit.ravenCustom > 0;
    const FlockCatalog::OuiSignature *oui = findOui(mac);
    bool tnSerial = false;
    bool xuntong = false;
    if (device->haveManufacturerData()) {
      std::string data = device->getManufacturerData();
      tnSerial = hasTnSerial(data);
      if (data.length() >= 2) xuntong = ((uint8_t)data[0] | ((uint8_t)data[1] << 8)) == 0x09C8;
    }

    hit.evidence = FlockCatalog::kCandidate;
    if (exactPenguin(hit.name)) {
      hit.evidence = FlockCatalog::kHigh;
      strlcpy(hit.identity, hit.name, sizeof(hit.identity));
      strlcpy(hit.method, "ble_penguin_identity", sizeof(hit.method));
      addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.name.penguin10");
    } else if (exactFlock(hit.name)) {
      bool suffix = flockSuffixMatches(hit.name, mac);
      hit.evidence = suffix ? FlockCatalog::kHigh : FlockCatalog::kMedium;
      strlcpy(hit.method, suffix ? "ble_flock_name_mac" : "ble_flock_name", sizeof(hit.method));
      addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.name.flock6");
      if (suffix) addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.name_mac_suffix");
    } else if (equalsIgnoreCase(hit.name, "FS Ext Battery")) {
      hit.evidence = tnSerial ? FlockCatalog::kHigh : FlockCatalog::kMedium;
      strlcpy(hit.method, "ble_battery_name", sizeof(hit.method));
      addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.name.battery");
    } else if (hit.ravenCustom > 0) {
      hit.evidence = hit.ravenCustom > 1 ? FlockCatalog::kHigh : FlockCatalog::kMedium;
      strlcpy(hit.method, hit.ravenCustom > 1 ? "ble_raven_multi" : "ble_raven_uuid", sizeof(hit.method));
      addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.raven.custom");
    } else {
      for (size_t index = 0; index < FlockCatalog::BLE_NAME_COUNT; ++index) {
        const FlockCatalog::NameSignature &item = FlockCatalog::BLE_NAMES[index];
        bool matched = item.prefix ? startsIgnoreCase(hit.name, item.value) : equalsIgnoreCase(hit.name, item.value);
        if (matched) {
          hit.evidence = item.evidence;
          strlcpy(hit.method, "ble_catalog_name", sizeof(hit.method));
          addSignature(hit.signatureIds, sizeof(hit.signatureIds), item.id);
          break;
        }
      }
    }
    if (tnSerial) {
      addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.mfg.tn_serial");
      if (hit.evidence == FlockCatalog::kCandidate && hit.name[0]) hit.evidence = FlockCatalog::kMedium;
    }
    if (xuntong) addSignature(hit.signatureIds, sizeof(hit.signatureIds), "ble.company.xuntong");
    if (oui) addSignature(hit.signatureIds, sizeof(hit.signatureIds), oui->id);
    if (!hit.signatureIds[0]) return;
    if (!hit.identity[0]) strlcpy(hit.identity, hit.mac, sizeof(hit.identity));
    hit.confidence = hit.evidence == FlockCatalog::kHigh ? 90 :
                     (hit.evidence == FlockCatalog::kMedium ? 70 : 35);
    hit.candidate = hit.evidence == FlockCatalog::kCandidate;
    hit.alertEligible = alertEligible(hit.evidence);
    snprintf(hit.extra, sizeof(hit.extra), "raven_custom=%u context=%u company_09c8=%u",
             hit.ravenCustom, hit.ravenContext, xuntong ? 1 : 0);
    queueBle(hit);
  }
};

static AggregatorBleCallbacks bleCallbacks;

static bool shouldEmit(const char *identity, bool &rediscovered) {
  uint32_t now = millis();
  rediscovered = false;
  for (RecentEmission &item : recent) {
    if (item.identity[0] && strcasecmp(item.identity, identity) == 0) {
      rediscovered = now - item.lastSeen > REDISCOVER_MS;
      item.lastSeen = now;
      if (!rediscovered && now - item.lastEmit < EMIT_COOLDOWN_MS) return false;
      item.lastEmit = now;
      return true;
    }
  }
  RecentEmission &item = recent[recentNext++ % (sizeof(recent) / sizeof(recent[0]))];
  strlcpy(item.identity, identity, sizeof(item.identity));
  item.lastSeen = item.lastEmit = now;
  return true;
}

static void emitCalibration(const BleDetection &hit) {
  if (!calibrationEnabled) return;
  uint32_t hash = 2166136261UL;
  for (const char *cursor = hit.identity; *cursor; ++cursor) { hash ^= (uint8_t)*cursor; hash *= 16777619UL; }
  JsonDocument doc;
  doc["event"] = "diag";
  doc["diag_type"] = "observation";
  doc["identity_hash"] = String(hash, HEX);
  doc["oui"] = hit.oui;
  doc["name_grammar"] = exactPenguin(hit.name) ? "penguin10" :
                         (exactFlock(hit.name) ? "flock6" :
                          (equalsIgnoreCase(hit.name, "FS Ext Battery") ? "battery_exact" : "other"));
  doc["rssi_bucket"] = (hit.rssi / 5) * 5;
  doc["raven_custom"] = hit.ravenCustom;
  doc["raven_context"] = hit.ravenContext;
  doc["match_flags"] = hit.signatureIds;
  sendPanel(doc, "ble-esp32");
}

static void drainBleQueue() {
  while (true) {
    portENTER_CRITICAL(&bleMux);
    if (bleTail == bleHead) { portEXIT_CRITICAL(&bleMux); break; }
    BleDetection hit;
    memcpy(&hit, (const void *)&bleQueue[bleTail], sizeof(hit));
    bleTail = (uint8_t)((bleTail + 1) % BLE_QUEUE_SIZE);
    portEXIT_CRITICAL(&bleMux);
    bool rediscovered = false;
    if (!shouldEmit(hit.identity, rediscovered)) continue;
    JsonDocument doc;
    doc["event"] = "detection";
    doc["mac_address"] = hit.mac;
    doc["oui"] = hit.oui;
    doc["protocol"] = "bluetooth_le";
    doc["capture"] = hit.raven ? "RAVEN_BLE" : "FLOCK_BLE";
    doc["detection_method"] = hit.method;
    doc["device_name"] = hit.name;
    doc["ssid"] = "";
    doc["rssi"] = hit.rssi;
    doc["channel"] = 0;
    doc["frequency"] = 0;
    doc["tx_power"] = hit.txPower;
    doc["confidence"] = hit.confidence;
    doc["confidence_label"] = hit.evidence == FlockCatalog::kHigh ? "HIGH" :
                              (hit.evidence == FlockCatalog::kMedium ? "MEDIUM" : "CAND");
    doc["catalog"] = FlockCatalog::HASH;
    doc["evidence"] = evidenceName(hit.evidence);
    doc["signature_ids"] = hit.signatureIds;
    doc["candidate"] = hit.candidate;
    doc["alert_eligible"] = hit.alertEligible;
    doc["direct_rssi"] = true;
    doc["identity"] = hit.identity;
    doc["extra"] = hit.extra;
    doc["raven_custom_uuid_count"] = hit.ravenCustom;
    doc["raven_context_uuid_count"] = hit.ravenContext;
    doc["rediscovered"] = rediscovered;
    sendPanel(doc, "ble-esp32");
    if (hit.raven) ++emittedRaven; else ++emittedBle;
    if (hit.alertEligible) { digitalWrite(DETECTION_LED_PIN, HIGH); ledOffMs = millis() + 120; }
    emitCalibration(hit);
  }
}

static void copyBwStatus(JsonDocument &doc) {
  strlcpy(bwStatus.radioPhase, doc["radio_phase"] | "unknown", sizeof(bwStatus.radioPhase));
  strlcpy(bwStatus.band, doc["band"] | "dual", sizeof(bwStatus.band));
  strlcpy(bwStatus.scanMode, doc["mode"] | "custom", sizeof(bwStatus.scanMode));
  bwStatus.framesMgmt = doc["wifi_mgmt_frames"] | (doc["wifi_mgmt"] | 0);
  bwStatus.framesData = doc["wifi_data_frames"] | (doc["wifi_data"] | 0);
  bwStatus.queueDrops = doc["queue_drops"] | 0;
  bwStatus.parserErrors = doc["parser_errors"] | 0;
  bwStatus.oversizedLines = doc["oversized_lines"] | 0;
  bwStatus.channelErrors = doc["channel_errors"] | 0;
  bwStatus.targetCycles = doc["target_cycles"] | 0;
  bwStatus.fullSweeps = doc["full_sweeps"] | 0;
  bwStatus.listen24Ms = doc["listen_24_ms"] | 0;
  bwStatus.listen5Ms = doc["listen_5_ms"] | 0;
  bwStatus.channel = doc["channel"] | 0;
  bwStatus.fallback = doc["fallback"] | false;
}

static void processBwLine(const char *line) {
  JsonDocument source;
  if (deserializeJson(source, line)) { ++bwParserErrors; return; }
  if ((int)(source["v"] | 0) != 1 || !source["event"].is<const char *>()) { ++bwParserErrors; return; }
  uint32_t sourceSequence = source["seq"] | 0;
  const char *event = source["event"] | "";
  if (!strcmp(event, "hello") && sourceSequence <= lastBw16Sequence) lastBw16Sequence = 0;
  if (sourceSequence == lastBw16Sequence) { ++bwDuplicate; return; }
  if (sourceSequence < lastBw16Sequence) { ++bwOutOfOrder; return; }
  lastBw16Sequence = sourceSequence;
  lastBw16RxMs = millis();
  if (!strcmp(event, "status") || !strcmp(event, "hello") || !strcmp(event, "diag")) copyBwStatus(source);
  source["source_seq"] = sourceSequence;
  source["seq"] = ++globalSequence;
  source["source"] = "wifi-bw16";
  serializeJson(source, PanelLink);
  PanelLink.write('\n');
}

static void sendStatus(const char *event, const char *message) {
  JsonDocument doc;
  doc["event"] = event;
  doc["message"] = message;
  doc["firmware"] = "cypher-flock-ble-aggregator/1.0";
  doc["catalog"] = FlockCatalog::HASH;
  doc["profile"] = profileName();
  doc["scan"] = scanPaused ? "paused" : "running";
  doc["diagnostics"] = diagnosticsEnabled;
  doc["calibration"] = calibrationEnabled;
  doc["aggregator_health"] = "online";
  doc["ble_health"] = scanPaused ? "paused" : healthFor(lastBleEngineMs);
  doc["bw16_health"] = healthFor(lastBw16RxMs);
  doc["ble_reports"] = bleReports;
  doc["ble_scan_starts"] = bleScanStarts;
  doc["emitted_ble"] = emittedBle;
  doc["emitted_raven"] = emittedRaven;
  doc["ble_queue_drops"] = bleQueueDrops;
  doc["panel_parser_errors"] = panelParserErrors;
  doc["panel_oversized_lines"] = panelOversized;
  doc["bw_parser_errors"] = bwParserErrors;
  doc["bw_oversized_lines"] = bwOversized;
  doc["bw_duplicates"] = bwDuplicate;
  doc["bw_out_of_order"] = bwOutOfOrder;
  doc["radio_phase"] = bwStatus.radioPhase;
  doc["band"] = bwStatus.band;
  doc["mode"] = bwStatus.scanMode;
  doc["channel"] = bwStatus.channel;
  doc["fallback"] = bwStatus.fallback;
  doc["wifi_mgmt_frames"] = bwStatus.framesMgmt;
  doc["wifi_data_frames"] = bwStatus.framesData;
  doc["queue_drops"] = bwStatus.queueDrops + bleQueueDrops;
  doc["parser_errors"] = bwStatus.parserErrors + bwParserErrors + panelParserErrors;
  doc["oversized_lines"] = bwStatus.oversizedLines + bwOversized + panelOversized;
  doc["channel_errors"] = bwStatus.channelErrors;
  doc["target_cycles"] = bwStatus.targetCycles;
  doc["full_sweeps"] = bwStatus.fullSweeps;
  doc["listen_24_ms"] = bwStatus.listen24Ms;
  doc["listen_5_ms"] = bwStatus.listen5Ms;
  sendPanel(doc, "ble-esp32");
  lastStatusMs = millis();
}

static void forwardBw(const String &command) {
  Bw16Link.println(command);
}

static void handleCommand(String command) {
  command.trim();
  String lower = command;
  lower.toLowerCase();
  if (!lower.length()) return;
  if (lower == "ping") sendAck("ping", true, "pong");
  else if (lower == "status") sendStatus("status", "requested snapshot");
  else if (lower == "catalog") {
    sendAck("catalog", true, String(FlockCatalog::VERSION) + "/" + FlockCatalog::HASH);
    forwardBw(command);
  } else if (lower.startsWith("profile ")) {
    String value = lower.substring(8);
    if (value == "precision") profileMode = kPrecision;
    else if (value == "balanced") profileMode = kBalanced;
    else if (value == "recall") profileMode = kRecall;
    else { sendAck("profile", false, "use precision|balanced|recall"); return; }
    sendAck("profile", true, value);
    forwardBw(command);
  } else if (lower.startsWith("diag ")) {
    diagnosticsEnabled = lower.endsWith("on");
    sendAck("diag", true, diagnosticsEnabled ? "on" : "off");
    forwardBw(command);
  } else if (lower.startsWith("calibration ")) {
    String value = lower.substring(12);
    if (value == "on") calibrationEnabled = true;
    else if (value == "off") calibrationEnabled = false;
    else if (value != "export" && value != "clear") { sendAck("calibration", false, "use on|off|export|clear"); return; }
    sendAck("calibration", true, value);
    forwardBw(command);
  } else if (lower == "scan pause" || lower == "scan resume") {
    scanPaused = lower.endsWith("pause");
    if (bleScan) {
      if (scanPaused) bleScan->stop();
      else { bleScan->start(0, false, true); ++bleScanStarts; lastBleEngineMs = millis(); }
    }
    sendAck("scan", true, scanPaused ? "paused" : "running");
    forwardBw(command);
  } else if (lower == "reset counters") {
    bleReports = bleScanStarts = emittedBle = emittedRaven = bleQueueDrops = 0;
    panelParserErrors = panelOversized = bwParserErrors = bwOversized = bwDuplicate = bwOutOfOrder = 0;
    memset(recent, 0, sizeof(recent));
    sendAck("reset", true, "aggregator counters cleared");
    forwardBw(command);
  } else if (lower.startsWith("band ") || lower.startsWith("mode ") ||
             lower.startsWith("channel ")) {
    forwardBw(command);
    sendAck("route", true, "forwarded to BW16");
  } else sendAck("command", false, "unknown command");
}

static void pollCommandStream(Stream &stream, char *buffer, size_t capacity,
                              size_t &length, bool &discarding, uint32_t &oversized) {
  while (stream.available()) {
    char value = (char)stream.read();
    if (value == '\r') continue;
    if (value == '\n') {
      if (!discarding && length) { buffer[length] = '\0'; handleCommand(String(buffer)); }
      length = 0;
      discarding = false;
    } else if (!discarding) {
      if (length < capacity - 1) buffer[length++] = value;
      else { length = 0; discarding = true; ++oversized; }
    }
  }
}

static void pollBw16() {
  while (Bw16Link.available()) {
    char value = (char)Bw16Link.read();
    if (value == '\r') continue;
    if (value == '\n') {
      if (!bwDiscarding && bwLineLen) { bwLine[bwLineLen] = '\0'; processBwLine(bwLine); }
      bwLineLen = 0;
      bwDiscarding = false;
    } else if (!bwDiscarding) {
      if (bwLineLen < MAX_BW16_LINE - 1) bwLine[bwLineLen++] = value;
      else { bwLineLen = 0; bwDiscarding = true; ++bwOversized; }
    }
  }
}

void setup() {
  pinMode(DETECTION_LED_PIN, OUTPUT);
  digitalWrite(DETECTION_LED_PIN, LOW);
  Serial.begin(115200);
  PanelLink.begin(LINK_BAUD, SERIAL_8N1, PANEL_RX_PIN, PANEL_TX_PIN);
  Bw16Link.begin(LINK_BAUD, SERIAL_8N1, BW16_RX_PIN, BW16_TX_PIN);
  WiFi.mode(WIFI_OFF);
  btStop();
  delay(50);
  NimBLEDevice::init("CypherFlockBLE");
  bleScan = NimBLEDevice::getScan();
  bleScan->setScanCallbacks(&bleCallbacks, true);
  bleScan->setActiveScan(false);
  bleScan->setInterval(80);
  bleScan->setWindow(79);
  bool started = bleScan->start(0, false, true);
  if (started) { ++bleScanStarts; lastBleEngineMs = millis(); }
  delay(100);
  sendStatus("hello", started ? "BLE aggregator ready; awaiting BW16" : "BLE scan failed to start");
  forwardBw("status");
}

void loop() {
  pollCommandStream(PanelLink, panelLine, sizeof(panelLine), panelLineLen, panelDiscarding, panelOversized);
  pollCommandStream(Serial, usbLine, sizeof(usbLine), usbLineLen, usbDiscarding, panelOversized);
  pollBw16();
  drainBleQueue();
  if (ledOffMs && millis() >= ledOffMs) { digitalWrite(DETECTION_LED_PIN, LOW); ledOffMs = 0; }
  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) sendStatus("status", "");
  if (diagnosticsEnabled && millis() - lastDiagMs >= DIAG_INTERVAL_MS) {
    sendStatus("diag", "periodic aggregator diagnostic snapshot");
    lastDiagMs = millis();
  }
  delay(1);
}
