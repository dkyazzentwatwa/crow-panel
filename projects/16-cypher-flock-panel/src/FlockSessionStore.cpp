#include "FlockSessionStore.h"
#include <CrowPanelShared.h>

#if USE_FLOCK_PERSISTENCE
#include <FFat.h>
#include <Preferences.h>
#endif

namespace {
constexpr const char *kDir = "/cypher-flock";
constexpr const char *kCurrent = "/cypher-flock/session.json";
constexpr const char *kTemp = "/cypher-flock/session.tmp";
constexpr const char *kPrevious = "/cypher-flock/previous.json";
constexpr const char *kCalibration = "/cypher-flock/calibration.ndjson";
constexpr const char *kPrefs = "cflock";

void copyField(char *dst, size_t len, JsonVariantConst value) {
  strlcpy(dst, value.is<const char *>() ? value.as<const char *>() : "", len);
}
}

uint32_t FlockSessionStore::crc32_(const uint8_t *data, size_t len) const {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
    }
  }
  return ~crc;
}

bool FlockSessionStore::validate_(const char *path, String *payload) const {
#if USE_FLOCK_PERSISTENCE
  File file = FFat.open(path, FILE_READ);
  if (!file) return false;
  String header = file.readStringUntil('\n');
  JsonDocument envelope;
  if (deserializeJson(envelope, header)) {
    file.close();
    return false;
  }
  int version = envelope["v"] | 1;
  if (version != 1 && version != 2) { file.close(); return false; }
  size_t bytes = envelope["bytes"] | 0;
  uint32_t expected = envelope["crc"] | 0;
  if (bytes == 0 || file.available() < (int)bytes) {
    file.close();
    return false;
  }
  String body;
  if (!body.reserve(bytes + 1)) {
    file.close();
    return false;
  }
  while (file.available() && body.length() < bytes) body += (char)file.read();
  file.close();
  if (body.length() != bytes || crc32_((const uint8_t *)body.c_str(), body.length()) != expected) {
    return false;
  }
  if (payload) *payload = body;
  return true;
#else
  (void)path;
  (void)payload;
  return false;
#endif
}

bool FlockSessionStore::copy_(const char *source, const char *destination) const {
#if USE_FLOCK_PERSISTENCE
  File in = FFat.open(source, FILE_READ);
  if (!in) return false;
  File out = FFat.open(destination, FILE_WRITE);
  if (!out) {
    in.close();
    return false;
  }
  uint8_t buffer[256];
  bool ok = true;
  while (in.available()) {
    size_t count = in.read(buffer, sizeof(buffer));
    if (count == 0 || out.write(buffer, count) != count) {
      ok = false;
      break;
    }
  }
  in.close();
  out.close();
  return ok;
#else
  (void)source;
  (void)destination;
  return false;
#endif
}

bool FlockSessionStore::load_(const char *path, FlockDetectionStore &store) const {
  String payload;
  if (!validate_(path, &payload)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, payload) || !doc.is<JsonArray>()) return false;
  store.clear();
  for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
    FlockDetection d;
    copyField(d.mac, sizeof(d.mac), item["mac"]);
    copyField(d.oui, sizeof(d.oui), item["oui"]);
    copyField(d.protocol, sizeof(d.protocol), item["protocol"]);
    copyField(d.capture, sizeof(d.capture), item["capture"]);
    copyField(d.method, sizeof(d.method), item["method"]);
    copyField(d.deviceName, sizeof(d.deviceName), item["name"]);
    copyField(d.ssid, sizeof(d.ssid), item["ssid"]);
    copyField(d.extra, sizeof(d.extra), item["extra"]);
    copyField(d.source, sizeof(d.source), item["source"]);
    copyField(d.band, sizeof(d.band), item["band"]);
    copyField(d.catalog, sizeof(d.catalog), item["catalog"]);
    copyField(d.evidence, sizeof(d.evidence), item["evidence"]);
    copyField(d.signatureIds, sizeof(d.signatureIds), item["signature_ids"]);
    copyField(d.identity, sizeof(d.identity), item["identity"]);
    copyField(d.label, sizeof(d.label), item["label"]);
    d.rssi = item["rssi"] | -127;
    d.channel = item["channel"] | 0;
    d.frequency = item["frequency"] | 0;
    d.txPower = item["tx_power"] | 0;
    d.confidence = item["confidence"] | 0;
    d.firstSeen = item["first"] | 0;
    d.lastSeen = item["last"] | 0;
    d.count = item["count"] | 1;
    d.rediscovered = item["rediscovered"] | false;
    d.candidate = item["candidate"] | false;
    d.alertEligible = item["alert_eligible"] | true;
    d.directRssi = item["direct_rssi"] | true;
    d.ravenCustomUuidCount = item["raven_custom"] | 0;
    d.ravenContextUuidCount = item["raven_context"] | 0;
    if (!d.identity[0]) strlcpy(d.identity, d.mac, sizeof(d.identity));
    if (!store.restore(d)) break;
  }
  return true;
}

bool FlockSessionStore::begin(FlockDetectionStore &previous, FlockLifetimeStats &lifetime,
                              String &status) {
#if USE_FLOCK_PERSISTENCE
  if (!FFat.begin(false)) {
    status = "FFat mount failed; RAM-only session";
    Logger::warn("flock-store", status);
    return false;
  }
  ready_ = true;
  if (!FFat.exists(kDir)) FFat.mkdir(kDir);
  if (FFat.exists(kCalibration)) {
    File calibration = FFat.open(kCalibration, FILE_READ);
    while (calibration && calibration.available() && calibrationCount_ < 0xFFFF) {
      if (calibration.read() == '\n') ++calibrationCount_;
    }
    if (calibration) calibration.close();
  }

  const char *validCurrent = validate_(kCurrent) ? kCurrent : (validate_(kTemp) ? kTemp : nullptr);
  if (validCurrent && copy_(validCurrent, kPrevious)) {
    previousReady_ = load_(kPrevious, previous);
  } else {
    previousReady_ = load_(kPrevious, previous);
  }
  if (FFat.exists(kCurrent)) FFat.remove(kCurrent);
  if (FFat.exists(kTemp)) FFat.remove(kTemp);

  Preferences prefs;
  if (prefs.begin(kPrefs, true)) {
    lifetime.wifi = prefs.getULong("wifi", 0);
    lifetime.ble = prefs.getULong("ble", 0);
    lifetime.raven = prefs.getULong("raven", 0);
    prefs.end();
  }
  status = previousReady_ ? "FFat ready; previous session loaded" : "FFat ready";
  Logger::info("flock-store", status);
  return true;
#else
  (void)previous;
  (void)lifetime;
  status = "persistence disabled; RAM-only session";
  return false;
#endif
}

bool FlockSessionStore::buildPayload_(const FlockDetectionStore &store, String &payload) const {
  if (!payload.reserve((size_t)store.count() * 360U + 32U)) return false;
  payload = "[";
  for (uint16_t i = 0; i < store.count(); ++i) {
    const FlockDetection *d = store.at(i);
    if (!d) continue;
    JsonDocument item;
    item["mac"] = d->mac;
    item["oui"] = d->oui;
    item["protocol"] = d->protocol;
    item["capture"] = d->capture;
    item["method"] = d->method;
    item["name"] = d->deviceName;
    item["ssid"] = d->ssid;
    item["extra"] = d->extra;
    item["source"] = d->source;
    item["band"] = d->band;
    item["catalog"] = d->catalog;
    item["evidence"] = d->evidence;
    item["signature_ids"] = d->signatureIds;
    item["identity"] = d->identity;
    item["rssi"] = d->rssi;
    item["channel"] = d->channel;
    item["frequency"] = d->frequency;
    item["tx_power"] = d->txPower;
    item["confidence"] = d->confidence;
    item["label"] = d->label;
    item["first"] = d->firstSeen;
    item["last"] = d->lastSeen;
    item["count"] = d->count;
    item["rediscovered"] = d->rediscovered;
    item["candidate"] = d->candidate;
    item["alert_eligible"] = d->alertEligible;
    item["direct_rssi"] = d->directRssi;
    item["raven_custom"] = d->ravenCustomUuidCount;
    item["raven_context"] = d->ravenContextUuidCount;
    if (i > 0) payload += ',';
    serializeJson(item, payload);
  }
  payload += ']';
  return true;
}

bool FlockSessionStore::save(const FlockDetectionStore &current,
                             const FlockLifetimeStats &lifetime, String &status) {
#if USE_FLOCK_PERSISTENCE
  if (!ready_) {
    status = "save skipped: FFat unavailable";
    return false;
  }
  String payload;
  if (!buildPayload_(current, payload)) {
    status = "save failed: payload allocation";
    return false;
  }
  uint32_t crc = crc32_((const uint8_t *)payload.c_str(), payload.length());
  File file = FFat.open(kTemp, FILE_WRITE);
  if (!file) {
    status = "save failed: cannot open temp file";
    return false;
  }
  JsonDocument envelope;
  envelope["v"] = 2;
  envelope["count"] = current.count();
  envelope["bytes"] = payload.length();
  envelope["crc"] = crc;
  serializeJson(envelope, file);
  file.write('\n');
  file.print(payload);
  file.close();
  if (!validate_(kTemp)) {
    status = "save failed: CRC validation";
    return false;
  }
  if (FFat.exists(kCurrent)) FFat.remove(kCurrent);
  if (!FFat.rename(kTemp, kCurrent) && !copy_(kTemp, kCurrent)) {
    status = "save failed: promote";
    return false;
  }
  if (FFat.exists(kTemp)) FFat.remove(kTemp);
  Preferences prefs;
  if (prefs.begin(kPrefs, false)) {
    prefs.putULong("wifi", lifetime.wifi);
    prefs.putULong("ble", lifetime.ble);
    prefs.putULong("raven", lifetime.raven);
    prefs.end();
  }
  lastSaveMs_ = millis();
  status = "session saved: " + String(current.count()) + " devices";
  return true;
#else
  (void)current;
  (void)lifetime;
  status = "save skipped: persistence disabled";
  return false;
#endif
}

bool FlockSessionStore::appendCalibration(JsonObjectConst observation, String &status) {
#if USE_FLOCK_PERSISTENCE
  if (!ready_) { status = "calibration RAM-only; observation not persisted"; return false; }
  if (calibrationCount_ >= 512) { status = "calibration cap reached"; return false; }
  File file = FFat.open(kCalibration, FILE_APPEND);
  if (!file) { status = "calibration append failed"; return false; }
  JsonDocument clean;
  const char *allowed[] = {"source", "identity_hash", "mac_hash", "oui", "name_grammar",
                           "company_id", "advertised_uuids", "role", "channel", "band",
                           "channel_bitset", "rssi_bucket", "match_flags", "raven_custom",
                           "raven_context", "ssid_flock_format", "wildcard"};
  for (const char *key : allowed) if (!observation[key].isNull()) clean[key] = observation[key];
  serializeJson(clean, file);
  file.write('\n');
  file.close();
  ++calibrationCount_;
  status = "calibration observation stored";
  return true;
#else
  (void)observation;
  status = "calibration persistence disabled";
  return false;
#endif
}

bool FlockSessionStore::exportCalibration(Stream &output, String &status) const {
#if USE_FLOCK_PERSISTENCE
  if (!ready_ || !FFat.exists(kCalibration)) { status = "no calibration export"; return false; }
  File file = FFat.open(kCalibration, FILE_READ);
  if (!file) { status = "calibration export open failed"; return false; }
  while (file.available()) output.write((uint8_t)file.read());
  file.close();
  status = "calibration export complete";
  return true;
#else
  (void)output;
  status = "calibration persistence disabled";
  return false;
#endif
}

bool FlockSessionStore::clearCalibration(String &status) {
#if USE_FLOCK_PERSISTENCE
  if (ready_ && FFat.exists(kCalibration)) FFat.remove(kCalibration);
  calibrationCount_ = 0;
  status = "calibration observations cleared";
  return true;
#else
  status = "calibration persistence disabled";
  return false;
#endif
}

bool FlockSessionStore::clearCurrent(String &status) {
#if USE_FLOCK_PERSISTENCE
  if (ready_) {
    if (FFat.exists(kCurrent)) FFat.remove(kCurrent);
    if (FFat.exists(kTemp)) FFat.remove(kTemp);
  }
  status = "current session cleared";
  return true;
#else
  status = "RAM session cleared";
  return true;
#endif
}
