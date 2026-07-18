#ifndef CYPHER_FLOCK_PANEL_TYPES_H
#define CYPHER_FLOCK_PANEL_TYPES_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"

enum FlockScreen : uint8_t {
  kFlockScopeScreen = 0,
  kFlockFeedScreen,
  kFlockWitnessScreen,
  kFlockStatsScreen,
  kFlockControlScreen,
  kFlockScreenCount
};

enum FlockFilter : uint8_t {
  kFlockFilterAll = 0,
  kFlockFilterWifi,
  kFlockFilterBle,
  kFlockFilterRaven,
  kFlockFilterCandidate,
  kFlockFilterBw16,
  kFlockFilterEsp32
};

enum FlockLinkState : uint8_t {
  kFlockLinkMock = 0,
  kFlockLinkOnline,
  kFlockLinkStale,
  kFlockLinkOffline
};

enum FlockSort : uint8_t {
  kFlockSortRecent = 0,
  kFlockSortSignal,
  kFlockSortConfidence
};

struct FlockDetection {
  char mac[18] = "";
  char oui[9] = "";
  char protocol[16] = "";
  char capture[16] = "";
  char method[48] = "";
  char deviceName[33] = "";
  char ssid[33] = "";
  char extra[48] = "";
  char source[16] = "";
  char band[8] = "";
  char catalog[16] = "";
  char evidence[12] = "candidate";
  char signatureIds[96] = "";
  char identity[33] = "";
  int8_t rssi = -127;
  uint8_t channel = 0;
  uint16_t frequency = 0;
  int16_t txPower = 0;
  uint8_t confidence = 0;
  char label[8] = "LOW";
  uint32_t firstSeen = 0;
  uint32_t lastSeen = 0;
  uint16_t count = 0;
  bool rediscovered = false;
  bool candidate = false;
  bool alertEligible = true;
  bool directRssi = true;
  uint8_t ravenCustomUuidCount = 0;
  uint8_t ravenContextUuidCount = 0;
};

struct FlockBridgeStatus {
  FlockLinkState link = USE_FLOCK_UART_BRIDGE ? kFlockLinkOffline : kFlockLinkMock;
  uint32_t lastRxMs = 0;
  uint32_t lastStatusMs = 0;
  uint32_t sequence = 0;
  uint32_t wifiMgmtFrames = 0;
  uint32_t wifiDataFrames = 0;
  uint32_t bleReports = 0;
  uint32_t queueDrops = 0;
  uint32_t parserErrors = 0;
  uint32_t oversizedLines = 0;
  uint32_t unknownVersions = 0;
  uint32_t duplicateLines = 0;
  uint32_t outOfOrderLines = 0;
  uint32_t bleQueueDrops = 0;
  uint32_t bwDuplicates = 0;
  uint32_t bwOutOfOrder = 0;
  uint32_t channelErrors = 0;
  uint32_t targetCycles = 0;
  uint32_t fullSweeps = 0;
  uint32_t listen24Ms = 0;
  uint32_t listen5Ms = 0;
  uint8_t channel = 1;
  char mode[12] = "full";
  char band[8] = "dual";
  char profile[12] = "balanced";
  char radioPhase[20] = "unknown";
  char scanState[12] = "running";
  FlockLinkState aggregatorLink = USE_FLOCK_UART_BRIDGE ? kFlockLinkOffline : kFlockLinkMock;
  FlockLinkState bleLink = USE_FLOCK_UART_BRIDGE ? kFlockLinkOffline : kFlockLinkMock;
  FlockLinkState bw16Link = USE_FLOCK_UART_BRIDGE ? kFlockLinkOffline : kFlockLinkMock;
  bool fallback = false;
  bool diagnostics = false;
};

struct FlockLifetimeStats {
  uint32_t wifi = 0;
  uint32_t ble = 0;
  uint32_t raven = 0;
};

inline bool flockIsWifi(const FlockDetection &d) {
  return strncmp(d.protocol, "wifi", 4) == 0;
}

inline bool flockIsRaven(const FlockDetection &d) {
  return strstr(d.capture, "RAVEN") != nullptr || strstr(d.method, "raven") != nullptr;
}

inline bool flockIsBle(const FlockDetection &d) {
  return strstr(d.protocol, "bluetooth") != nullptr;
}

#endif
