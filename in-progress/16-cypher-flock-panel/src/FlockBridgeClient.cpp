#include "FlockBridgeClient.h"
#include <CrowPanelShared.h>

namespace {
void copyJsonString(char *dst, size_t len, JsonVariantConst value, const char *fallback = "") {
  if (value.is<const char *>()) {
    strlcpy(dst, value.as<const char *>(), len);
  } else if (fallback && fallback != dst) {
    strlcpy(dst, fallback, len);
  }
}
}

void FlockBridgeClient::begin(Stream *link, FlockDetectionHandler detectionHandler,
                              FlockEventHandler eventHandler,
                              FlockCalibrationHandler calibrationHandler) {
  link_ = link;
  detectionHandler_ = detectionHandler;
  eventHandler_ = eventHandler;
  calibrationHandler_ = calibrationHandler;
#if USE_FLOCK_UART_BRIDGE
  Logger::info("flock-link", "UART bridge enabled; waiting for hello/status");
#else
  status_.link = kFlockLinkMock;
  Logger::info("flock-link", "mock detector source active");
#endif
}

const char *FlockBridgeClient::driverName() const {
#if USE_FLOCK_UART_BRIDGE
  return "esp32-uart";
#else
  return "mock";
#endif
}

void FlockBridgeClient::tick() {
#if USE_FLOCK_UART_BRIDGE
  if (link_ != nullptr) {
    while (link_->available() > 0) {
      char c = (char)link_->read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (discarding_) {
          discarding_ = false;
        } else if (lineLen_ > 0) {
          line_[lineLen_] = '\0';
          parseLine_(line_);
        }
        lineLen_ = 0;
        continue;
      }
      if (discarding_) continue;
      if (lineLen_ >= FLOCK_UART_MAX_LINE - 1) {
        lineLen_ = 0;
        discarding_ = true;
        ++status_.oversizedLines;
        if (eventHandler_) eventHandler_("error", "oversized bridge line dropped");
        continue;
      }
      line_[lineLen_++] = c;
    }
  }
  updateLinkState_();
#endif
}

bool FlockBridgeClient::inject(const String &line) {
  if (line.length() == 0) return false;
  if (line.length() >= FLOCK_UART_MAX_LINE) {
    ++status_.oversizedLines;
    if (eventHandler_) eventHandler_("error", "oversized injected line dropped");
    return false;
  }
  return parseLine_(line.c_str());
}

bool FlockBridgeClient::parseLine_(const char *line) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, line);
  if (error) {
    ++status_.parserErrors;
    if (eventHandler_) eventHandler_("error", error.c_str());
    return false;
  }
  if (!doc["v"].is<int>() || doc["v"].as<int>() != 1) {
    ++status_.unknownVersions;
    if (eventHandler_) eventHandler_("error", "unsupported bridge protocol version");
    return false;
  }
  uint32_t incomingSequence = doc["seq"] | 0;
  const char *incomingEvent = doc["event"] | "";
  if (strcmp(incomingEvent, "hello") == 0 && incomingSequence <= status_.sequence) {
    status_.sequence = 0;
  }
  if (incomingSequence && incomingSequence == status_.sequence) {
    ++status_.duplicateLines;
    if (eventHandler_) eventHandler_("error", "duplicate bridge sequence dropped");
    return false;
  }
  if (incomingSequence && incomingSequence < status_.sequence) {
    ++status_.outOfOrderLines;
    if (eventHandler_) eventHandler_("error", "out-of-order bridge sequence dropped");
    return false;
  }
  status_.lastRxMs = millis();
  status_.sequence = incomingSequence ? incomingSequence : status_.sequence;
  handleDocument_(doc);
  return true;
}

void FlockBridgeClient::handleDocument_(JsonDocument &doc) {
  const char *event = doc["event"] | "";
  if (strcmp(event, "detection") == 0) {
    FlockDetection d;
    copyJsonString(d.mac, sizeof(d.mac), doc["mac_address"]);
    copyJsonString(d.oui, sizeof(d.oui), doc["oui"]);
    copyJsonString(d.protocol, sizeof(d.protocol), doc["protocol"]);
    copyJsonString(d.capture, sizeof(d.capture), doc["capture"]);
    copyJsonString(d.method, sizeof(d.method), doc["detection_method"]);
    copyJsonString(d.deviceName, sizeof(d.deviceName), doc["device_name"]);
    copyJsonString(d.ssid, sizeof(d.ssid), doc["ssid"]);
    copyJsonString(d.extra, sizeof(d.extra), doc["extra"]);
    copyJsonString(d.source, sizeof(d.source), doc["source"]);
    copyJsonString(d.band, sizeof(d.band), doc["band"]);
    copyJsonString(d.catalog, sizeof(d.catalog), doc["catalog"]);
    copyJsonString(d.evidence, sizeof(d.evidence), doc["evidence"], "candidate");
    copyJsonString(d.signatureIds, sizeof(d.signatureIds), doc["signature_ids"]);
    copyJsonString(d.identity, sizeof(d.identity), doc["identity"]);
    copyJsonString(d.label, sizeof(d.label), doc["confidence_label"], "LOW");
    d.rssi = doc["rssi"] | -127;
    d.channel = doc["channel"] | 0;
    d.frequency = doc["frequency"] | 0;
    d.txPower = doc["tx_power"] | 0;
    d.confidence = doc["confidence"] | 0;
    d.candidate = doc["candidate"] | false;
    d.alertEligible = doc["alert_eligible"] | true;
    d.directRssi = doc["direct_rssi"] | true;
    d.rediscovered = doc["rediscovered"] | false;
    d.ravenCustomUuidCount = doc["raven_custom_uuid_count"] | 0;
    d.ravenContextUuidCount = doc["raven_context_uuid_count"] | 0;
    if (!d.identity[0]) strlcpy(d.identity, d.mac, sizeof(d.identity));
    if (d.mac[0] == '\0') {
      ++status_.parserErrors;
      if (eventHandler_) eventHandler_("error", "detection missing MAC");
      return;
    }
    if (detectionHandler_) detectionHandler_(d);
    return;
  }

  if (strcmp(event, "hello") == 0 || strcmp(event, "status") == 0 ||
      strcmp(event, "diag") == 0) {
    if (strcmp(event, "diag") == 0 && strcmp(doc["diag_type"] | "", "observation") == 0) {
      if (calibrationHandler_) calibrationHandler_(doc.as<JsonObjectConst>());
      return;
    }
    status_.link = kFlockLinkOnline;
    status_.lastStatusMs = millis();
    status_.channel = doc["channel"] | status_.channel;
    copyJsonString(status_.mode, sizeof(status_.mode), doc["mode"], status_.mode);
    copyJsonString(status_.band, sizeof(status_.band), doc["band"], status_.band);
    copyJsonString(status_.profile, sizeof(status_.profile), doc["profile"], status_.profile);
    copyJsonString(status_.radioPhase, sizeof(status_.radioPhase), doc["radio_phase"], status_.radioPhase);
    copyJsonString(status_.scanState, sizeof(status_.scanState), doc["scan"], status_.scanState);
    status_.diagnostics = doc["diagnostics"] | status_.diagnostics;
    status_.wifiMgmtFrames = doc["wifi_mgmt_frames"] | (doc["wifi_mgmt"] | status_.wifiMgmtFrames);
    status_.wifiDataFrames = doc["wifi_data_frames"] | (doc["wifi_data"] | status_.wifiDataFrames);
    status_.bleReports = doc["ble_reports"] | status_.bleReports;
    status_.queueDrops = doc["queue_drops"] | status_.queueDrops;
    status_.parserErrors = doc["parser_errors"] | status_.parserErrors;
    status_.oversizedLines = doc["oversized_lines"] | status_.oversizedLines;
    status_.bleQueueDrops = doc["ble_queue_drops"] | status_.bleQueueDrops;
    status_.bwDuplicates = doc["bw_duplicates"] | status_.bwDuplicates;
    status_.bwOutOfOrder = doc["bw_out_of_order"] | status_.bwOutOfOrder;
    status_.channelErrors = doc["channel_errors"] | status_.channelErrors;
    status_.targetCycles = doc["target_cycles"] | status_.targetCycles;
    status_.fullSweeps = doc["full_sweeps"] | status_.fullSweeps;
    status_.listen24Ms = doc["listen_24_ms"] | status_.listen24Ms;
    status_.listen5Ms = doc["listen_5_ms"] | status_.listen5Ms;
    status_.fallback = doc["fallback"] | status_.fallback;
    auto parseHealth = [](const char *value) {
      if (!strcmp(value, "online")) return kFlockLinkOnline;
      if (!strcmp(value, "stale")) return kFlockLinkStale;
      if (!strcmp(value, "paused")) return kFlockLinkStale;
      return kFlockLinkOffline;
    };
    if (!doc["aggregator_health"].isNull()) status_.aggregatorLink = parseHealth(doc["aggregator_health"] | "offline");
    if (!doc["ble_health"].isNull()) status_.bleLink = parseHealth(doc["ble_health"] | "offline");
    if (!doc["bw16_health"].isNull()) status_.bw16Link = parseHealth(doc["bw16_health"] | "offline");
  }
  if (eventHandler_) eventHandler_(event, doc["message"] | "");
}

void FlockBridgeClient::updateLinkState_() {
  if (status_.lastStatusMs == 0) {
    status_.link = kFlockLinkOffline;
    return;
  }
  uint32_t age = millis() - status_.lastStatusMs;
  if (age >= FLOCK_BRIDGE_OFFLINE_MS) status_.link = kFlockLinkOffline;
  else if (age >= FLOCK_BRIDGE_STALE_MS) status_.link = kFlockLinkStale;
  else status_.link = kFlockLinkOnline;
}

bool FlockBridgeClient::sendCommand(const String &command) {
#if USE_FLOCK_UART_BRIDGE
  if (!link_ || command.length() == 0 || command.indexOf('\n') >= 0 || command.indexOf('\r') >= 0) {
    return false;
  }
  link_->println(command);
  return true;
#else
  if (eventHandler_) eventHandler_("ack", (String("mock command: ") + command).c_str());
  return true;
#endif
}
