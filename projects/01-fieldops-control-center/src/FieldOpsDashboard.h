#ifndef FIELDOPS_DASHBOARD_H
#define FIELDOPS_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "SensorNode.h"

// Full operations dashboard for the CrowPanel display: header status bar, a
// live node roster down the left, battery/temperature ring gauges, a
// temperature-trend sparkline, an alert banner, and a footer ticker.
//
// Nodes are keyed by name and discovered dynamically, so the same dashboard
// serves the 4 fixed mock/LoRa nodes AND an open ESP-NOW mesh (sensor nodes
// with telemetry + chat nodes as presence-only tiles).
//
// Everything is gated on USE_DISPLAY. When the display is off, every method
// is a no-op so the sketch still runs Serial-only. Data arrives through
// onPacket/onAlert/onSummary (driven by the sketch's processPacket); a dirty
// flag coalesces a single per-band repaint in tick(), so no full-screen flash
// occurs and presence frames refresh even though they skip the summary step.
class FieldOpsDashboard {
 public:
  void begin();
  void onPacket(const SensorPacket &packet);  // upsert node (telemetry or presence)
  void onAlert(const String &alert);          // sets the active alert
  void onSummary(const String &summary);      // sets AI summary
  void tick();                                // touch select + coalesced repaint + clock

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
 private:
  static const uint8_t kMaxNodes = 8;
  static const uint8_t kVisibleCards = 5;
  static const uint16_t kHist = 48;
  static const unsigned long kStaleMs = 60000;  // no packet for 60s => stale

  struct NodeState {
    String name;
    bool used = false;
    bool hasTelemetry = false;
    unsigned long lastSeenMs = 0;
    int rssi = 0;
    float tempC = 0;
    float humidityPct = 0;
    float batteryPct = 0;
    bool motion = false;
    float tempHist[kHist];
    uint16_t histCount = 0;
    uint16_t histHead = 0;
  };

  void paintChrome();
  void repaint();
  void drawHeaderStatus();
  void drawRoster();
  void drawNodeCard(uint8_t slot, int8_t nodeIdx);  // nodeIdx < 0 => empty slot
  void drawBanner();
  void drawGauges();
  void drawStats();
  void drawSparkline();
  void drawFooter();

  int8_t findOrAddNode(const String &name);
  int8_t activeNode() const;
  bool isStale(const NodeState &n) const;
  uint8_t visibleNodes(int8_t *out) const;  // fills up to kVisibleCards indices, newest first

  NodeState nodes_[kMaxNodes];
  uint8_t nodeCount_ = 0;
  int8_t lastNode_ = -1;
  int8_t pinnedNode_ = -1;  // -1 = auto-follow the newest

  String alert_ = "";
  String summary_ = "conditions nominal";
  uint32_t packetCount_ = 0;

  bool ready_ = false;
  bool dirty_ = false;
  bool wasTouched_ = false;
#endif
};

#endif
