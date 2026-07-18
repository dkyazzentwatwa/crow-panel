// Cypher Flock headless dual-band Wi-Fi node for Ai-Thinker BW16/RTL8720DN.
// Receive-only: raw 802.11 promiscuous capture with passive beacon fallback.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FlockSignatureCatalog.h>
#include "wifi_conf.h"
#include "wifi_constants.h"
#include "wifi_structures.h"
#include "wifi_util.h"

#ifndef USE_BW16_PROMISCUOUS
#define USE_BW16_PROMISCUOUS 1
#endif

#define LINK_BAUD 115200
#define MAX_LINK_LINE 192
#define RAW_QUEUE_SIZE 16
#define RAW_SNAPSHOT_BYTES 144
#define STATUS_INTERVAL_MS 2000UL
#define DIAG_INTERVAL_MS 10000UL
#define FULL_SWEEP_INTERVAL_MS 60000UL
#define WILDCARD_WINDOW_MS 10000UL
#define EMIT_COOLDOWN_MS 5000UL

extern "C" void LwIP_Init(void);

enum ScanMode : uint8_t { kModeFull = 0, kModeCustom, kModeSingle };
enum BandMode : uint8_t { kBand24 = 0, kBand5, kBandDual };
enum ProfileMode : uint8_t { kPrecision = 0, kBalanced, kRecall };

struct RawFrame {
  uint16_t length;
  int8_t rssi;
  uint8_t channel;
  uint8_t bytes[RAW_SNAPSHOT_BYTES];
};

struct WildcardTrack {
  uint8_t mac[6];
  uint64_t channelBits;
  uint32_t firstMs;
  uint32_t lastMs;
  uint8_t distinctChannels;
};

struct RecentEmission {
  uint8_t mac[6];
  uint32_t lastMs;
};

// AmebaD's Arduino preprocessor can mis-detect static return types. Keep every
// sketch function explicitly declared so compilation never depends on ctags.
static const char *modeName();
static const char *bandName();
static const char *profileName();
static const char *evidenceName(FlockCatalog::EvidenceTier evidence);
static uint8_t confidenceFor(FlockCatalog::EvidenceTier evidence);
static bool alertEligible(FlockCatalog::EvidenceTier evidence);
static void sendDocument(JsonDocument &doc);
static void sendAck(const char *command, bool ok, const String &message);
static void macToString(const uint8_t *mac, char *out, size_t size);
static void ouiToString(const uint8_t *mac, char *out, size_t size);
static uint32_t hashMac(const uint8_t *mac);
static bool isMulticast(const uint8_t *mac);
static bool equalIgnoreCase(const char *left, const char *right, size_t count);
static void appendText(char *target, size_t capacity, const char *text);
static const FlockCatalog::OuiSignature *findOui(const uint8_t *mac);
static bool exactFlockSsid(const char *ssid);
static bool ssidSuffixMatches(const char *ssid, const uint8_t *mac);
static bool extractSsid(const uint8_t *body, int length, char *ssid, size_t capacity);
static int wildcardProbe(const uint8_t *body, int length);
static int8_t channelOrdinal(uint8_t channel);
static uint8_t noteWildcard(const uint8_t *mac, uint8_t channel);
static bool shouldEmit(const uint8_t *mac);
static uint16_t channelFrequency(uint8_t channel);
static void emitCalibration(const uint8_t *mac, const char *role, uint8_t channel,
                            int8_t rssi, FlockCatalog::EvidenceTier evidence,
                            bool ssidMatch, bool wildcard);
static void emitDetection(const uint8_t *mac, int8_t rssi, uint8_t channel,
                          const char *frameKind, const char *method,
                          FlockCatalog::EvidenceTier evidence, const char *signatureIds,
                          const char *ssid, bool directRssi);
static void processAddressMatch(const uint8_t *mac, int8_t rssi, uint8_t channel,
                                const char *role, bool directRssi);
static void processRawFrame(const RawFrame &frame);
static void rawCallback(unsigned char *buffer, unsigned int length, void *userData);
static bool channelMatchesBand(uint8_t channel);
static uint8_t selectedCount(bool full);
static uint8_t selectedAt(bool full, uint8_t ordinal);
static uint16_t dwellFor(uint8_t channel);
static bool setChannelChecked(uint8_t channel);
static void resetChannelPlan();
static void channelTick();
static bool startFallback();
static bool startRaw();
static void drainRawQueue();
static void fallbackTick();
static bool runPassiveFallback();
static void listenTimeTick();
static void sendStatus(const char *event, const char *message);
static bool validSingleChannel(int channel);
static void handleCommand(String command);
static void pollCommands(Stream &input, char *buffer, uint8_t &length, bool &discarding);
void setup();
void loop();

static volatile RawFrame rawQueue[RAW_QUEUE_SIZE];
static volatile uint8_t rawHead = 0;
static volatile uint8_t rawTail = 0;
static volatile uint32_t rawQueueDrops = 0;
static volatile uint32_t wifiMgmtFrames = 0;
static volatile uint32_t wifiDataFrames = 0;
static volatile uint8_t currentChannel = 1;

static const uint8_t targetChannels[] = {1, 6, 11, 36, 40, 44, 48, 149, 153, 157, 161, 165};
static const uint8_t fullChannels[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};

static WildcardTrack wildcardTracks[16] = {};
static RecentEmission recent[24] = {};
static uint8_t recentNext = 0;
static uint32_t sequenceNumber = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastDiagMs = 0;
static uint32_t lastHopMs = 0;
static uint32_t lastFullSweepMs = 0;
static uint32_t listen24Ms = 0;
static uint32_t listen5Ms = 0;
static uint32_t channelErrors = 0;
static uint32_t targetCycles = 0;
static uint32_t fullSweeps = 0;
static uint32_t parserErrors = 0;
static uint32_t oversizedLines = 0;
static uint32_t emittedDetections = 0;
static uint32_t candidateDetections = 0;
static uint32_t suppressedCandidates = 0;
static uint32_t fallbackScans = 0;
static uint32_t fallbackFailures = 0;
static uint32_t lastListenTickMs = 0;
static uint32_t lastFallbackScanMs = 0;
static uint8_t channelIndex = 0;
static uint8_t singleChannel = 1;
static ScanMode scanMode = kModeCustom;
static BandMode bandMode = kBandDual;
static ProfileMode profileMode = kBalanced;
static bool scanPaused = false;
static bool diagnosticsEnabled = false;
static bool calibrationEnabled = false;
static bool rawReady = false;
static bool fallbackReady = false;
static bool fullSweepActive = false;
static char linkLine[MAX_LINK_LINE] = {};
static uint8_t linkLineLen = 0;
static bool linkDiscarding = false;
static char usbLine[MAX_LINK_LINE] = {};
static uint8_t usbLineLen = 0;
static bool usbDiscarding = false;

static const char *modeName() {
  if (scanMode == kModeFull) return "full";
  if (scanMode == kModeSingle) return "single";
  return "custom";
}

static const char *bandName() {
  if (bandMode == kBand24) return "2.4";
  if (bandMode == kBand5) return "5";
  return "dual";
}

static const char *profileName() {
  if (profileMode == kPrecision) return "precision";
  if (profileMode == kRecall) return "recall";
  return "balanced";
}

static const char *evidenceName(FlockCatalog::EvidenceTier evidence) {
  if (evidence == FlockCatalog::kHigh) return "high";
  if (evidence == FlockCatalog::kMedium) return "medium";
  return "candidate";
}

static uint8_t confidenceFor(FlockCatalog::EvidenceTier evidence) {
  if (evidence == FlockCatalog::kHigh) return 90;
  if (evidence == FlockCatalog::kMedium) return 70;
  return 35;
}

static bool alertEligible(FlockCatalog::EvidenceTier evidence) {
  if (evidence == FlockCatalog::kHigh) return true;
  if (evidence == FlockCatalog::kMedium) return profileMode != kPrecision;
  return profileMode == kRecall;
}

static void sendDocument(JsonDocument &doc) {
  doc["v"] = 1;
  doc["seq"] = ++sequenceNumber;
  doc["source"] = "wifi-bw16";
  serializeJson(doc, Serial1);
  Serial1.write('\n');
}

static void sendAck(const char *command, bool ok, const String &message) {
  JsonDocument doc;
  doc["event"] = ok ? "ack" : "error";
  doc["command"] = command;
  doc["ok"] = ok;
  doc["message"] = message;
  sendDocument(doc);
  Serial.print("[bw16:");
  Serial.print(ok ? "ack" : "error");
  Serial.print("] command=");
  Serial.print(command);
  Serial.print(" message=");
  Serial.println(message);
}

static void macToString(const uint8_t *mac, char *out, size_t size) {
  snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
}

static void ouiToString(const uint8_t *mac, char *out, size_t size) {
  snprintf(out, size, "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
}

static uint32_t hashMac(const uint8_t *mac) {
  uint32_t hash = 2166136261u;
  for (uint8_t i = 0; i < 6; ++i) hash = (hash ^ mac[i]) * 16777619u;
  return hash;
}

static bool isMulticast(const uint8_t *mac) { return (mac[0] & 0x01) != 0; }

static bool equalIgnoreCase(const char *left, const char *right, size_t count) {
  if (!left || !right) return false;
  for (size_t index = 0; index < count; ++index) {
    char a = left[index];
    char b = right[index];
    if (a >= 'a' && a <= 'z') a -= ('a' - 'A');
    if (b >= 'a' && b <= 'z') b -= ('a' - 'A');
    if (a != b) return false;
  }
  return true;
}

static void appendText(char *target, size_t capacity, const char *text) {
  if (!target || !text || capacity == 0) return;
  size_t used = strlen(target);
  if (used >= capacity - 1) return;
  size_t available = capacity - used - 1;
  strncat(target, text, available);
}

static const FlockCatalog::OuiSignature *findOui(const uint8_t *mac) {
  if (!mac || isMulticast(mac)) return nullptr;
  for (size_t i = 0; i < FlockCatalog::WIFI_OUI_COUNT; ++i) {
    const FlockCatalog::OuiSignature &entry = FlockCatalog::WIFI_OUIS[i];
    if (mac[0] == entry.bytes[0] && mac[1] == entry.bytes[1] && mac[2] == entry.bytes[2]) {
      return &entry;
    }
  }
  return nullptr;
}

static bool exactFlockSsid(const char *ssid) {
  if (!ssid || strncmp(ssid, "Flock-", 6) != 0 || strlen(ssid) != 12) return false;
  for (uint8_t i = 6; i < 12; ++i) {
    if (!isxdigit((unsigned char)ssid[i]) || islower((unsigned char)ssid[i])) return false;
  }
  return true;
}

static bool ssidSuffixMatches(const char *ssid, const uint8_t *mac) {
  if (!exactFlockSsid(ssid)) return false;
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  return equalIgnoreCase(ssid + 6, suffix, 6);
}

static bool extractSsid(const uint8_t *body, int length, char *ssid, size_t capacity) {
  while (body && length >= 2) {
    uint8_t id = body[0];
    uint8_t itemLength = body[1];
    if ((int)itemLength + 2 > length) return false;
    if (id == 0) {
      size_t copyLength = min((size_t)itemLength, capacity - 1);
      memcpy(ssid, body + 2, copyLength);
      ssid[copyLength] = '\0';
      return true;
    }
    body += itemLength + 2;
    length -= itemLength + 2;
  }
  return false;
}

static int wildcardProbe(const uint8_t *body, int length) {
  while (body && length >= 2) {
    uint8_t id = body[0];
    uint8_t itemLength = body[1];
    if ((int)itemLength + 2 > length) return -1;
    if (id == 0) return itemLength == 0 ? 1 : 0;
    body += itemLength + 2;
    length -= itemLength + 2;
  }
  return -1;
}

static int8_t channelOrdinal(uint8_t channel) {
  for (uint8_t i = 0; i < sizeof(fullChannels); ++i) if (fullChannels[i] == channel) return i;
  return -1;
}

static uint8_t noteWildcard(const uint8_t *mac, uint8_t channel) {
  uint32_t now = millis();
  WildcardTrack *slot = nullptr;
  for (WildcardTrack &entry : wildcardTracks) {
    if (memcmp(entry.mac, mac, 6) == 0) { slot = &entry; break; }
    if (!slot && entry.firstMs == 0) slot = &entry;
  }
  if (!slot) slot = &wildcardTracks[hashMac(mac) % 16];
  if (slot->firstMs == 0 || memcmp(slot->mac, mac, 6) != 0 || now - slot->lastMs > WILDCARD_WINDOW_MS) {
    memset(slot, 0, sizeof(*slot));
    memcpy(slot->mac, mac, 6);
    slot->firstMs = now;
  }
  slot->lastMs = now;
  int8_t ordinal = channelOrdinal(channel);
  if (ordinal >= 0) {
    uint64_t bit = 1ULL << ordinal;
    if ((slot->channelBits & bit) == 0) {
      slot->channelBits |= bit;
      ++slot->distinctChannels;
    }
  }
  return slot->distinctChannels;
}

static bool shouldEmit(const uint8_t *mac) {
  uint32_t now = millis();
  for (RecentEmission &entry : recent) {
    if (memcmp(entry.mac, mac, 6) == 0) {
      if (now - entry.lastMs < EMIT_COOLDOWN_MS) return false;
      entry.lastMs = now;
      return true;
    }
  }
  RecentEmission &entry = recent[recentNext++ % 24];
  memcpy(entry.mac, mac, 6);
  entry.lastMs = now;
  return true;
}

static uint16_t channelFrequency(uint8_t channel) {
  if (channel >= 1 && channel <= 13) return 2407 + channel * 5;
  if (channel == 14) return 2484;
  if (channel >= 32) return 5000 + channel * 5;
  return 0;
}

static void emitCalibration(const uint8_t *mac, const char *role, uint8_t channel,
                            int8_t rssi, FlockCatalog::EvidenceTier evidence,
                            bool ssidMatch, bool wildcard) {
  if (!calibrationEnabled) return;
  char oui[9];
  ouiToString(mac, oui, sizeof(oui));
  char hash[12];
  snprintf(hash, sizeof(hash), "%08lx", (unsigned long)hashMac(mac));
  JsonDocument doc;
  doc["event"] = "diag";
  doc["diag_type"] = "observation";
  doc["mac_hash"] = hash;
  doc["oui"] = oui;
  doc["role"] = role;
  doc["channel"] = channel;
  doc["band"] = channel >= 32 ? "5" : "2.4";
  doc["rssi_bucket"] = (rssi / 5) * 5;
  doc["evidence"] = evidenceName(evidence);
  doc["ssid_flock_format"] = ssidMatch;
  doc["wildcard"] = wildcard;
  sendDocument(doc);
}

static void emitDetection(const uint8_t *mac, int8_t rssi, uint8_t channel,
                          const char *frameKind, const char *method,
                          FlockCatalog::EvidenceTier evidence, const char *signatureIds,
                          const char *ssid, bool directRssi) {
  if (!shouldEmit(mac)) return;
  bool candidate = evidence == FlockCatalog::kCandidate;
  bool eligible = alertEligible(evidence);
  if (candidate) ++candidateDetections;
  if (!eligible) ++suppressedCandidates;
  char macText[18];
  char oui[9];
  macToString(mac, macText, sizeof(macText));
  ouiToString(mac, oui, sizeof(oui));
  JsonDocument doc;
  doc["event"] = "detection";
  doc["mac_address"] = macText;
  doc["oui"] = oui;
  doc["protocol"] = channel >= 32 ? "wifi_5ghz" : "wifi_2_4ghz";
  doc["capture"] = "FLOCK_WIFI";
  doc["detection_method"] = method;
  doc["device_name"] = "";
  doc["ssid"] = ssid ? ssid : "";
  doc["extra"] = frameKind ? frameKind : "";
  doc["rssi"] = rssi;
  doc["channel"] = channel;
  doc["frequency"] = channelFrequency(channel);
  doc["tx_power"] = 0;
  doc["confidence"] = confidenceFor(evidence);
  doc["confidence_label"] = evidence == FlockCatalog::kHigh ? "HIGH" :
                            (evidence == FlockCatalog::kMedium ? "MEDIUM" : "CAND");
  doc["catalog"] = FlockCatalog::HASH;
  doc["evidence"] = evidenceName(evidence);
  doc["signature_ids"] = signatureIds;
  doc["candidate"] = candidate;
  doc["alert_eligible"] = eligible;
  doc["direct_rssi"] = directRssi;
  doc["identity"] = macText;
  doc["band"] = channel >= 32 ? "5" : "2.4";
  sendDocument(doc);
  ++emittedDetections;
  emitCalibration(mac, method, channel, rssi, evidence, ssid && exactFlockSsid(ssid),
                  strstr(method, "wildcard") != nullptr);
}

static void processAddressMatch(const uint8_t *mac, int8_t rssi, uint8_t channel,
                                const char *role, bool directRssi) {
  const FlockCatalog::OuiSignature *oui = findOui(mac);
  if (!oui) return;
  FlockCatalog::EvidenceTier evidence = oui->standalone ? FlockCatalog::kHigh : FlockCatalog::kCandidate;
  char method[32];
  snprintf(method, sizeof(method), "wifi_oui_%s", role);
  emitDetection(mac, rssi, channel, role, method, evidence, oui->id, "", directRssi);
}

static void processRawFrame(const RawFrame &frame) {
  if (frame.length < 24) return;
  const uint8_t *bytes = frame.bytes;
  uint16_t frameControl = bytes[0] | ((uint16_t)bytes[1] << 8);
  uint8_t type = (frameControl >> 2) & 0x03;
  uint8_t subtype = (frameControl >> 4) & 0x0f;
  if (type == 0) ++wifiMgmtFrames;
  else if (type == 2) ++wifiDataFrames;
  else return;
  const uint8_t *addr1 = bytes + 4;
  const uint8_t *addr2 = bytes + 10;
  const uint8_t *addr3 = bytes + 16;
  const FlockCatalog::OuiSignature *sourceOui = findOui(addr2);
  bool emittedSource = false;
  char ssid[33] = {};
  const char *frameKind = type == 2 ? "data" : "management";

  if (type == 0) {
    const uint8_t *body = nullptr;
    int bodyLength = 0;
    if ((subtype == 8 || subtype == 5) && frame.length > 36) {
      body = bytes + 36;
      bodyLength = frame.length - 36;
      frameKind = subtype == 8 ? "beacon" : "probe_resp";
    } else if (subtype == 4 && frame.length > 24) {
      body = bytes + 24;
      bodyLength = frame.length - 24;
      frameKind = "probe_req";
    }
    if (body) {
      bool haveSsid = extractSsid(body, bodyLength, ssid, sizeof(ssid));
      if (haveSsid && exactFlockSsid(ssid)) {
        bool suffix = ssidSuffixMatches(ssid, addr2);
        FlockCatalog::EvidenceTier evidence = suffix ? FlockCatalog::kHigh : FlockCatalog::kMedium;
        char ids[96] = "wifi.ssid.flock6";
        if (suffix) appendText(ids, sizeof(ids), ",wifi.ssid_mac_suffix");
        if (sourceOui) { appendText(ids, sizeof(ids), ","); appendText(ids, sizeof(ids), sourceOui->id); }
        emitDetection(addr2, frame.rssi, frame.channel, frameKind, "wifi_flock_ssid", evidence,
                      ids, ssid, true);
        emittedSource = true;
      }
      if (subtype == 4 && sourceOui && wildcardProbe(body, bodyLength) == 1) {
        uint8_t channels = noteWildcard(addr2, frame.channel);
        FlockCatalog::EvidenceTier evidence = FlockCatalog::kCandidate;
        if (channels >= 2) {
          bool fieldOui = addr2[0] == 0x82 && addr2[1] == 0x6b && addr2[2] == 0xf2;
          evidence = fieldOui ? FlockCatalog::kHigh : FlockCatalog::kMedium;
        }
        char ids[96];
        snprintf(ids, sizeof(ids), "%s,wifi.wildcard_multichannel", sourceOui->id);
        emitDetection(addr2, frame.rssi, frame.channel, frameKind, "wifi_wildcard_probe",
                      evidence, ids, "", true);
        emittedSource = true;
      }
    }
  }

  if (sourceOui && !emittedSource) processAddressMatch(addr2, frame.rssi, frame.channel, "addr2", true);
  if (findOui(addr1)) processAddressMatch(addr1, frame.rssi, frame.channel, "addr1", false);
  if (type == 0 && memcmp(addr3, addr2, 6) != 0 && findOui(addr3)) {
    processAddressMatch(addr3, frame.rssi, frame.channel, "addr3", false);
  }
}

static void rawCallback(unsigned char *buffer, unsigned int length, void *userData) {
  if (!buffer || !userData || length < 24 || scanPaused) return;
  uint8_t next = (uint8_t)((rawHead + 1) % RAW_QUEUE_SIZE);
  if (next == rawTail) { ++rawQueueDrops; return; }
  RawFrame *entry = (RawFrame *)&rawQueue[rawHead];
  entry->length = min(length, (unsigned int)RAW_SNAPSHOT_BYTES);
  entry->rssi = ((ieee80211_frame_info_t *)userData)->rssi;
  entry->channel = currentChannel;
  memcpy((void *)entry->bytes, buffer, entry->length);
  rawHead = next;
}

static bool channelMatchesBand(uint8_t channel) {
  if (bandMode == kBand24) return channel < 32;
  if (bandMode == kBand5) return channel >= 32;
  return true;
}

static uint8_t selectedCount(bool full) {
  const uint8_t *channels = full ? fullChannels : targetChannels;
  uint8_t length = full ? sizeof(fullChannels) : sizeof(targetChannels);
  uint8_t count = 0;
  for (uint8_t i = 0; i < length; ++i) if (channelMatchesBand(channels[i])) ++count;
  return count;
}

static uint8_t selectedAt(bool full, uint8_t ordinal) {
  const uint8_t *channels = full ? fullChannels : targetChannels;
  uint8_t length = full ? sizeof(fullChannels) : sizeof(targetChannels);
  uint8_t found = 0;
  for (uint8_t i = 0; i < length; ++i) {
    if (!channelMatchesBand(channels[i])) continue;
    if (found++ == ordinal) return channels[i];
  }
  return bandMode == kBand5 ? 36 : 1;
}

static uint16_t dwellFor(uint8_t channel) {
  if (fullSweepActive) return 120;
  if (channel == 1 || channel == 6 || channel == 11) return 400;
  return 250;
}

static bool setChannelChecked(uint8_t channel) {
  int result = wifi_set_channel(channel);
  if (result != RTW_SUCCESS) {
    ++channelErrors;
    Serial.print("[bw16] channel=");
    Serial.print(channel);
    Serial.print(" status=fail rc=");
    Serial.println(result);
    return false;
  }
  currentChannel = channel;
  return true;
}

static void resetChannelPlan() {
  channelIndex = 0;
  fullSweepActive = scanMode == kModeFull;
  uint8_t channel = scanMode == kModeSingle ? singleChannel : selectedAt(fullSweepActive, 0);
  setChannelChecked(channel);
  lastHopMs = millis();
}

static void channelTick() {
  if (!rawReady || scanPaused || scanMode == kModeSingle) return;
  uint32_t now = millis();
  if (now - lastHopMs < dwellFor(currentChannel)) return;
  bool useFull = scanMode == kModeFull || fullSweepActive;
  uint8_t count = selectedCount(useFull);
  if (count == 0) return;
  ++channelIndex;
  if (channelIndex >= count) {
    channelIndex = 0;
    if (scanMode == kModeCustom && fullSweepActive) {
      fullSweepActive = false;
      lastFullSweepMs = now;
    } else if (scanMode == kModeCustom) {
      ++targetCycles;
      if (now - lastFullSweepMs >= FULL_SWEEP_INTERVAL_MS) {
        fullSweepActive = true;
        ++fullSweeps;
      }
    }
    useFull = scanMode == kModeFull || fullSweepActive;
    count = selectedCount(useFull);
  }
  setChannelChecked(selectedAt(useFull, channelIndex % max((uint8_t)1, count)));
  lastHopMs = now;
}

static bool startFallback() {
  wifi_set_promisc(RTW_PROMISC_DISABLE, nullptr, 0);
  LwIP_Init();
  if (wifi_on(RTW_MODE_STA) < 0) return false;
  fallbackReady = true;
  rawReady = false;
  return true;
}

static bool startRaw() {
#if USE_BW16_PROMISCUOUS
  wifi_enter_promisc_mode();
  int result = wifi_set_promisc(RTW_PROMISC_ENABLE_2, rawCallback, 1);
  if (result == RTW_SUCCESS) {
    rawReady = true;
    fallbackReady = false;
    resetChannelPlan();
    return true;
  }
  Serial.print("[bw16] promiscuous init failed rc=");
  Serial.print(result);
  Serial.println("; entering scan fallback");
#endif
  return startFallback();
}

static void drainRawQueue() {
  while (rawTail != rawHead) {
    RawFrame frame;
    memcpy(&frame, (const void *)&rawQueue[rawTail], sizeof(frame));
    rawTail = (uint8_t)((rawTail + 1) % RAW_QUEUE_SIZE);
    processRawFrame(frame);
  }
}

static void fallbackTick() {
  if (!fallbackReady || scanPaused) return;
  if (millis() - lastFallbackScanMs < 10000UL) return;
  lastFallbackScanMs = millis();
  if (runPassiveFallback()) ++fallbackScans;
  else ++fallbackFailures;
}

static bool runPassiveFallback() {
  // wifi_scan_networks() hardcodes RTW_SCAN_TYPE_ACTIVE and sends probes.
  // Use the lower-level passive scan buffer so fallback remains receive-only.
  static char buffer[4096];
  memset(buffer, 0, sizeof(buffer));
  scan_buf_arg scanBuffer = {buffer, (int)sizeof(buffer)};
  int result = wifi_scan(RTW_SCAN_TYPE_PASSIVE, RTW_BSS_TYPE_ANY, &scanBuffer);
  if (result < 0) return false;
  size_t offset = 0;
  while (offset + 14 <= sizeof(buffer)) {
    uint8_t recordLength = (uint8_t)buffer[offset];
    if (recordLength == 0) break;
    if (recordLength < 14 || offset + recordLength > sizeof(buffer)) {
      ++parserErrors;
      break;
    }
    const uint8_t *bssid = (const uint8_t *)(buffer + offset + 1);
    int rssiValue = -127;
    memcpy(&rssiValue, buffer + offset + 7, sizeof(rssiValue));
    uint8_t channel = (uint8_t)buffer[offset + 13];
    size_t ssidLength = recordLength - 14;
    char ssid[33] = {};
    memcpy(ssid, buffer + offset + 14, min(ssidLength, sizeof(ssid) - 1));
    const FlockCatalog::OuiSignature *oui = findOui(bssid);
    if (exactFlockSsid(ssid)) {
      FlockCatalog::EvidenceTier evidence = ssidSuffixMatches(ssid, bssid) ?
          FlockCatalog::kHigh : FlockCatalog::kMedium;
      char ids[96] = "wifi.ssid.flock6";
      if (evidence == FlockCatalog::kHigh) appendText(ids, sizeof(ids), ",wifi.ssid_mac_suffix");
      emitDetection(bssid, (int8_t)constrain(rssiValue, -127, 0), channel, "passive_scan",
                    "wifi_flock_ssid", evidence, ids, ssid, true);
    } else if (oui) {
      emitDetection(bssid, (int8_t)constrain(rssiValue, -127, 0), channel, "passive_scan",
                    "wifi_oui_scan", oui->standalone ? FlockCatalog::kHigh : FlockCatalog::kCandidate,
                    oui->id, ssid, true);
    }
    offset += recordLength;
  }
  return true;
}

static void listenTimeTick() {
  uint32_t now = millis();
  if (lastListenTickMs == 0) lastListenTickMs = now;
  uint32_t elapsed = now - lastListenTickMs;
  lastListenTickMs = now;
  if (scanPaused || !rawReady) return;
  if (currentChannel >= 32) listen5Ms += elapsed;
  else listen24Ms += elapsed;
}

static void sendStatus(const char *event, const char *message) {
  JsonDocument doc;
  doc["event"] = event;
  doc["message"] = message;
  doc["device"] = "cypher-flock-bw16";
  doc["catalog"] = FlockCatalog::HASH;
  doc["catalog_version"] = FlockCatalog::VERSION;
  doc["scan"] = scanPaused ? "paused" : "running";
  doc["mode"] = modeName();
  doc["band"] = bandName();
  doc["profile"] = profileName();
  doc["channel"] = currentChannel;
  doc["radio_phase"] = rawReady ? (fullSweepActive ? "full-sweep" : "target-hop") : "scan-fallback";
  doc["fallback"] = !rawReady;
  doc["diagnostics"] = diagnosticsEnabled;
  doc["calibration"] = calibrationEnabled;
  doc["wifi_mgmt"] = wifiMgmtFrames;
  doc["wifi_data"] = wifiDataFrames;
  doc["listen_24_ms"] = listen24Ms;
  doc["listen_5_ms"] = listen5Ms;
  doc["channel_errors"] = channelErrors;
  doc["target_cycles"] = targetCycles;
  doc["full_sweeps"] = fullSweeps;
  doc["queue_drops"] = rawQueueDrops;
  doc["parser_errors"] = parserErrors;
  doc["oversized_lines"] = oversizedLines;
  doc["emitted_wifi"] = emittedDetections;
  doc["candidate_events"] = candidateDetections;
  doc["suppressed_candidates"] = suppressedCandidates;
  doc["fallback_scans"] = fallbackScans;
  doc["fallback_failures"] = fallbackFailures;
  sendDocument(doc);
  lastStatusMs = millis();
}

static bool validSingleChannel(int channel) {
  if (channel >= 1 && channel <= 13) return true;
  for (uint8_t value : targetChannels) if (value >= 32 && value == channel) return true;
  return false;
}

static void handleCommand(String command) {
  command.trim();
  String lower = command;
  lower.toLowerCase();
  if (lower == "ping") sendAck("ping", true, "pong");
  else if (lower == "status") sendStatus("status", "");
  else if (lower == "catalog") sendAck("catalog", true, String(FlockCatalog::VERSION) + "/" + FlockCatalog::HASH);
  else if (lower == "diag" || lower == "diag on" || lower == "diag off") {
    if (lower.endsWith(" on")) diagnosticsEnabled = true;
    else if (lower.endsWith(" off")) diagnosticsEnabled = false;
    sendAck("diag", true, diagnosticsEnabled ? "on" : "off");
    sendStatus("diag", "diagnostic snapshot");
  } else if (lower.startsWith("profile ")) {
    String value = lower.substring(8);
    if (value == "precision") profileMode = kPrecision;
    else if (value == "balanced") profileMode = kBalanced;
    else if (value == "recall") profileMode = kRecall;
    else { sendAck("profile", false, "use precision|balanced|recall"); return; }
    sendAck("profile", true, profileName());
  } else if (lower.startsWith("band ")) {
    String value = lower.substring(5);
    if (value == "2.4") bandMode = kBand24;
    else if (value == "5") bandMode = kBand5;
    else if (value == "dual") bandMode = kBandDual;
    else { sendAck("band", false, "use 2.4|5|dual"); return; }
    resetChannelPlan();
    sendAck("band", true, bandName());
  } else if (lower.startsWith("mode ")) {
    String value = lower.substring(5);
    if (value == "full") scanMode = kModeFull;
    else if (value == "custom") scanMode = kModeCustom;
    else if (value == "single") scanMode = kModeSingle;
    else { sendAck("mode", false, "use full|custom|single"); return; }
    resetChannelPlan();
    sendAck("mode", true, modeName());
  } else if (lower.startsWith("channel ")) {
    int channel = lower.substring(8).toInt();
    if (!validSingleChannel(channel)) { sendAck("channel", false, "unsupported channel"); return; }
    singleChannel = channel;
    scanMode = kModeSingle;
    bool ok = rawReady ? setChannelChecked(singleChannel) : true;
    sendAck("channel", ok, ok ? String(channel) : "channel set failed");
  } else if (lower == "scan pause" || lower == "scan resume") {
    scanPaused = lower.endsWith("pause");
    sendAck("scan", true, scanPaused ? "paused" : "running");
  } else if (lower.startsWith("calibration ")) {
    String value = lower.substring(12);
    if (value == "on") calibrationEnabled = true;
    else if (value == "off") calibrationEnabled = false;
    else if (value != "export" && value != "clear") {
      sendAck("calibration", false, "use on|off|export|clear"); return;
    }
    sendAck("calibration", true, value == "export" ? "observations stream to panel" : value);
  } else if (lower == "reset counters") {
    wifiMgmtFrames = wifiDataFrames = rawQueueDrops = 0;
    listen24Ms = listen5Ms = channelErrors = targetCycles = fullSweeps = 0;
    emittedDetections = candidateDetections = suppressedCandidates = 0;
    parserErrors = oversizedLines = fallbackScans = fallbackFailures = 0;
    memset(recent, 0, sizeof(recent));
    memset(wildcardTracks, 0, sizeof(wildcardTracks));
    sendAck("reset", true, "BW16 counters cleared");
  } else sendAck("command", false, "unknown command");
}

static void pollCommands(Stream &input, char *buffer, uint8_t &length, bool &discarding) {
  while (input.available()) {
    char c = (char)input.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (!discarding && length > 0) { buffer[length] = '\0'; handleCommand(String(buffer)); }
      length = 0;
      discarding = false;
      continue;
    }
    if (discarding) continue;
    if (length < MAX_LINK_LINE - 1) buffer[length++] = c;
    else { length = 0; discarding = true; ++oversizedLines; }
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(LINK_BAUD);
  delay(250);
  Serial.println("[bw16] Cypher Flock dual-band Wi-Fi node");
  Serial.print("[bw16] catalog=");
  Serial.print(FlockCatalog::VERSION);
  Serial.print('/');
  Serial.print(FlockCatalog::HASH);
  Serial.println(" link=Serial1 115200");
  bool radioOk = startRaw();
  Serial.print("[bw16] radio=");
  Serial.print(rawReady ? "promiscuous" : "scan-fallback");
  Serial.print(" status=");
  Serial.println(radioOk ? "ready" : "failed");
  sendStatus("hello", radioOk ? "BW16 Wi-Fi node ready" : "BW16 radio initialization failed");
}

void loop() {
  pollCommands(Serial1, linkLine, linkLineLen, linkDiscarding);
  pollCommands(Serial, usbLine, usbLineLen, usbDiscarding);
  listenTimeTick();
  channelTick();
  drainRawQueue();
  fallbackTick();
  if (millis() - lastStatusMs >= STATUS_INTERVAL_MS) sendStatus("status", "");
  if (diagnosticsEnabled && millis() - lastDiagMs >= DIAG_INTERVAL_MS) {
    sendStatus("diag", "periodic BW16 diagnostic snapshot");
    lastDiagMs = millis();
  }
  delay(1);
}
