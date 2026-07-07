#ifndef RELAYOPS_CONTROL_HUB_DASHBOARD_H
#define RELAYOPS_CONTROL_HUB_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "HubTypes.h"
#include "WorldFeed.h"

// Operations dashboard for the CrowPanel display, adapted from FieldOps: a
// header status bar, a live roster down the left mixing SENSOR nodes and
// controllable ACTUATOR devices, battery/temperature ring gauges + a
// temperature sparkline for the selected sensor, and a footer ticker.
//
// Roster entries are keyed by name and discovered dynamically. Sensor entries
// arrive via onSensor (a node POST or the mock source); actuator entries via
// onDevice (seeded from config or self-registered, refreshed after each
// toggle). Tap a sensor card to pin its telemetry into the gauges; tap an
// actuator card to toggle it - the tap is queued and drained by the sketch
// through takePendingToggle(), which calls DeviceController::setPin().
//
// Everything is gated on USE_DISPLAY: with the display off every method is a
// no-op so the sketch still runs Serial-only. A dirty flag coalesces repaints.
class ControlHubDashboard {
 public:
  void begin();
  void onSensor(const SensorReading &reading);  // upsert sensor (telemetry or presence)
  void onDevice(const ControlDevice &device);   // upsert actuator tile
  void onEvent(const String &message);          // banner + footer ticker
  void onWorldFeeds(const WorldFeeds &feeds);   // latest weather/quake/aurora
  void tick();                                  // touch select/toggle + repaint + clock

  // Drains a queued actuator tap. Returns true and fills deviceId/desiredOn
  // when a tile was tapped since the last call. Always false with the
  // display off (no touch surface).
  bool takePendingToggle(String &deviceId, bool &desiredOn);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
 private:
  static const uint8_t kMaxEntries = 8;
  static const uint8_t kVisibleCards = 5;
  static const uint16_t kHist = 48;
  static const unsigned long kStaleMs = 60000;  // no update for 60s => stale

  enum EntryKind { kSensor = 0, kActuator = 1 };

  struct Entry {
    String name;
    EntryKind kind = kSensor;
    bool used = false;
    unsigned long lastSeenMs = 0;
    // Sensor fields.
    bool hasTelemetry = false;
    int rssi = 0;
    float tempC = 0;
    float humidityPct = 0;
    float batteryPct = 0;
    bool motion = false;
    float tempHist[kHist];
    uint16_t histCount = 0;
    uint16_t histHead = 0;
    // Actuator fields.
    bool on = false;
    bool online = true;
    String host;
    uint8_t pin = 0;
  };

  void paintChrome();
  void repaint();
  void drawHeaderStatus();
  void drawRoster();
  void drawCard(uint8_t slot, int8_t entryIdx);  // entryIdx < 0 => empty slot
  void drawSensorCard(int16_t y, const Entry &e, bool active, bool stale);
  void drawActuatorCard(int16_t y, const Entry &e, bool active, bool stale);
  void drawBanner();
  void drawDetail();    // gauges for a sensor, control panel for an actuator
  void drawStats();
  void drawSparkline();
  void drawFooter();

  int8_t findOrAddEntry(const String &name, EntryKind kind);
  int8_t activeEntry() const;
  bool isStale(const Entry &e) const;
  uint8_t visibleEntries(int8_t *out) const;  // up to kVisibleCards, newest first
  uint8_t countKind(EntryKind kind) const;

  Entry entries_[kMaxEntries];
  uint8_t entryCount_ = 0;
  int8_t lastSensor_ = -1;
  int8_t pinned_ = -1;  // -1 = auto-follow the newest sensor

  String banner_ = "hub online";
  uint32_t sensorEvents_ = 0;
  WorldFeeds world_;

  String pendingId_ = "";
  bool pendingOn_ = false;
  bool hasPending_ = false;

  bool ready_ = false;
  bool dirty_ = false;
  bool wasTouched_ = false;
#endif
};

#endif
