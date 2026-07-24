#ifndef FIELDOPS_DASHBOARD_H
#define FIELDOPS_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowTouch, Widgets, CrowDisplay, HardwareProfile
#include "SensorNode.h"

// Bespoke touch UI for the CrowPanel's 1024x600 DSI panel. Four screens keyed
// off an enum, a shared headerBar()/tabBar() chrome, and a dirty_ flag that
// coalesces one flush() per changed frame (no full-screen flash every loop):
//
//   ROSTER  - grid of field-node cards; tap a card to pin it (jumps to DETAIL)
//   DETAIL  - arcGauge() ring gauges for the pinned node + a temperature
//             sparkline() history
//   ALERTS  - warning/critical stream; tap an alert to acknowledge it
//   LOG     - scrollable rolling event log with prev/next paging controls
//
// The data model (nodes, alert store, rolling log, current screen, pinned
// node, log page) and all logic compile in EVERY build so the headless
// `selftest` can drive the whole flow with no panel attached. Only the drawing
// and touch-reading paths are gated on USE_DISPLAY. Nodes are discovered
// dynamically by name, so the same UI serves the 4 mock/LoRa nodes AND an open
// ESP-NOW mesh (sensor nodes as telemetry cards, chat nodes as presence tiles).
//
// tick() reads touch and returns a typed event for the sketch to execute; the
// UI never writes the shared EventLog/storage itself.

enum FieldOpsScreen : uint8_t {
  FIELDOPS_SCR_ROSTER = 0,
  FIELDOPS_SCR_DETAIL,
  FIELDOPS_SCR_ALERTS,
  FIELDOPS_SCR_LOG,
  FIELDOPS_SCR_COUNT,
};

enum FieldOpsUiEventType : uint8_t {
  FIELDOPS_EVT_NONE = 0,
  FIELDOPS_EVT_PIN_NODE,   // a roster card was pinned (index = model slot)
  FIELDOPS_EVT_ACK_ALERT,  // an alert was acknowledged (index = alert slot)
};

// Returned by tick(); `label` carries the node name / alert text so the sketch
// can record the action in the shared event log.
struct FieldOpsUiEvent {
  FieldOpsUiEventType type = FIELDOPS_EVT_NONE;
  int16_t index = -1;
  char label[48] = {0};
};

class FieldOpsDashboard {
 public:
  void begin();

  // Data in (driven by the sketch's processPacket()).
  void onPacket(const SensorPacket &packet);  // upsert node (telemetry/presence)
  void onAlert(const String &alert);          // append to the alert stream
  void onSummary(const String &summary);      // latest AI shift summary
  void note(const String &line);              // push one line into the rolling log

  // Reads touch, updates view state, and repaints when dirty. Returns true and
  // fills `event` when the user launched an app-domain action (pin / ack).
  bool tick(FieldOpsUiEvent &event);

  // --- Serial-parity actions: identical effect to the touch controls ---
  bool setScreen(FieldOpsScreen screen);          // true if the screen changed
  bool pinNodeByIndex(int8_t modelIdx);           // pin a node by model slot
  int8_t pinNodeByName(const String &name);       // returns model slot, or -1
  bool ackAlertByIndex(int8_t alertIdx);          // ack one stored ring slot
  int8_t ackAlertByDisplay(uint8_t displayIdx);   // ack by on-screen row (0=newest)
  int8_t ackNewestAlert();                        // ack newest unacked; -1 none
  void logPagePrev();                             // page toward newer entries
  void logPageNext();                             // page toward older entries
  bool setLogPage(uint16_t page);

  // --- Introspection for `status` / `selftest` ---
  FieldOpsScreen screen() const { return screen_; }
  const char *screenName() const;
  uint8_t nodeCount() const { return nodeCount_; }
  int8_t activeNodeIndex() const;
  String activeNodeName() const;
  uint8_t alertCount() const { return alertCount_; }
  uint8_t unackedAlertCount() const;
  uint16_t logCount() const { return logCount_; }
  uint32_t logPushes() const { return logPushes_; }  // monotonic (never wraps)
  uint16_t logPage() const { return logPage_; }
  uint16_t logPageCount() const;

  void printTouch(Print &out) const;  // the `touch` diagnostic command

 private:
  // ---- Data model: compiled in ALL builds (drives the headless selftest) ----
  static const uint8_t kMaxNodes = 8;
  static const uint8_t kRosterCards = 6;   // 2 columns x 3 rows
  static const uint16_t kHist = 48;
  static const unsigned long kStaleMs = 60000;  // no packet for 60s => stale
  static const uint8_t kMaxAlerts = 12;
  static const uint8_t kAlertRows = 6;     // alert rows visible at once
  static const uint8_t kLogCap = 48;
  static const uint8_t kLogLineLen = 60;
  static const uint8_t kLogPerPage = 8;

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

  struct AlertItem {
    char text[48] = {0};
    char node[16] = {0};
    uint8_t severity = 0;  // 0 = warning (amber), 1 = critical (red)
    unsigned long tsMs = 0;
    bool acked = false;
    bool used = false;
  };

  int8_t findOrAddNode(const String &name);
  bool isStale(const NodeState &n) const;
  uint8_t visibleNodes(int8_t *out, uint8_t maxOut) const;  // newest first
  int8_t alertRingFromDisplay(uint8_t displayIdx) const;    // 0 = newest
  void pushLog(const String &line);
  static uint8_t severityOf(const String &alert);
  static void nodeFromAlert(const String &alert, char *out, uint8_t outLen);

  NodeState nodes_[kMaxNodes];
  uint8_t nodeCount_ = 0;
  int8_t lastNode_ = -1;
  int8_t pinnedNode_ = -1;  // -1 = auto-follow the newest telemetry node

  AlertItem alerts_[kMaxAlerts];
  uint8_t alertCount_ = 0;  // saturates at kMaxAlerts
  uint8_t alertHead_ = 0;   // next ring slot to write

  char log_[kLogCap][kLogLineLen];
  unsigned long logTs_[kLogCap] = {0};
  uint16_t logCount_ = 0;   // saturates at kLogCap
  uint16_t logHead_ = 0;    // next ring slot to write
  uint32_t logPushes_ = 0;  // total lines ever pushed (never wraps)
  uint16_t logPage_ = 0;    // 0 = newest page

  String summary_ = "conditions nominal";
  uint32_t packetCount_ = 0;

  FieldOpsScreen screen_ = FIELDOPS_SCR_ROSTER;

  CrowTouch touch_;      // debounced GT911 (never-pressed stub on headless)
  bool ready_ = false;
  bool dirty_ = true;
  uint32_t lastDrawMs_ = 0;

  // ---- Rendering: display builds only ----
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  bool handleTouch_(FieldOpsUiEvent &event);
  void draw_();
  void drawHeader_();
  void drawTabs_();
  void drawRoster_();
  void drawNodeCard_(uint8_t slot, int8_t modelIdx);
  void drawDetail_();
  void drawAlerts_();
  void drawLog_();
#endif
};

#endif
