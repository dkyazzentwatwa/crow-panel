#ifndef RELAYOPS_CONTROL_HUB_DASHBOARD_H
#define RELAYOPS_CONTROL_HUB_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <WorldFeed.h>
#include <TouchInput.h>
#include "HubTypes.h"

// Multi-screen, touch-first control surface for the CrowPanel's 1024x600 DSI
// panel, drawn entirely through the shared Widgets:: toolkit (dark ops palette,
// FreeSans fonts, headerBar/tabBar chrome). Five screens, keyed by an enum and
// navigated by the bottom tab strip:
//
//   DEVICES  - grid of controllable devices; tap a card to toggle its relay/GPIO
//   DETAIL   - one device: state, HTTP target URL, last command/result, ON/OFF/TOGGLE
//   SENSORS  - incoming node readings with battery/temp gauges + a temp sparkline
//   WORLD    - weather / earthquake / aurora / air feed cards + a REFRESH button
//   EVENTS   - the recent event log
//
// tick() reads debounced touch and returns a typed HubUiEvent describing the
// action the user launched (kHubNone most frames). The sketch executes it
// against the real app objects (DeviceController / WorldFeedClient) and reflects
// the outcome back through onDevice()/onWorldFeeds(); the UI never mutates
// application state itself. A dirty flag coalesces repaints into one manual
// CrowDisplay::flush() per frame.
//
// Everything that draws or hit-tests is gated on USE_DISPLAY: with the display
// off every renderer is a no-op so the sketch still runs Serial-only, driven by
// the mock source, `feed`, and `set`.

// --- Screens (shared by both builds so screenName()/`screen` work headless). ---
enum HubScreen : uint8_t {
  HUB_DEVICES = 0,
  HUB_DETAIL,
  HUB_SENSORS,
  HUB_WORLD,
  HUB_EVENTS,
  HUB_SCREEN_COUNT,
};

// --- Typed UI action the dashboard hands back to the sketch each frame. ---
enum HubUiActionType : uint8_t {
  kHubNone = 0,
  kHubSetDevice,     // command a device: deviceId + on (toggle/on/off)
  kHubRefreshWorld,  // re-pull the world feeds
};

struct HubUiEvent {
  HubUiActionType type = kHubNone;
  String deviceId;
  bool on = false;
};

class ControlHubDashboard {
 public:
  void begin();
  void onSensor(const SensorReading &reading);  // upsert a sensor (telemetry or presence)
  void onDevice(const ControlDevice &device);   // upsert an actuator mirror
  void onEvent(const String &message);          // push onto the event log
  void onWorldFeeds(const WorldFeeds &feeds);   // latest weather/quake/aurora/air
  HubUiEvent tick();                            // touch + repaint; returns an action

  // Serial parity / diagnostics. screenName() and selectScreenByName() work in
  // both builds; printTouch() reports live points on a display build and a clear
  // note otherwise.
  void printTouch(Print &out) const;
  const char *screenName() const;
  bool selectScreenByName(const String &name);
  // Pin a sensor node into the gauges/sparkline by name (serial parity for the
  // sensor-list tap). Display build only; headless has no on-screen list.
  bool selectSensorByName(const String &name);

 private:
  HubScreen screen_ = HUB_DEVICES;
  CrowTouch touch_;  // shared debounced touch (never-pressed stub when headless)

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  static const uint8_t kMaxSensors = 6;
  static const uint8_t kMaxDevices = 8;
  static const uint8_t kEventCap = 12;
  static const uint16_t kHist = 48;
  static const unsigned long kStaleMs = 60000;                    // sensor age -> stale
  static const unsigned long kWorldStaleMs = 45UL * 60UL * 1000;  // feed dim threshold

  struct Sensor {
    bool used = false;
    String name;
    unsigned long lastSeenMs = 0;
    bool hasTelemetry = false;
    int rssi = 0;
    float tempC = 0;
    float humidityPct = 0;
    float batteryPct = 0;
    bool motion = false;
    float tempHist[kHist];
    uint16_t histCount = 0;
    uint16_t histHead = 0;
  };

  struct Device {
    bool used = false;
    String id;
    String host;
    String path;
    uint8_t pin = 0;
    bool state = false;
    bool online = true;
    unsigned long lastSeenMs = 0;
    // Detail-screen "last command / result" trail.
    String lastCmd;          // "SET ON" / "SET OFF" / "" when none yet
    String lastResult;       // "ok" / "unreachable" / "" when pending/none
    unsigned long lastCmdMs = 0;
    bool awaitingResult = false;
  };

  // --- touch handling (returns an action, may change screen_) ---
  HubUiEvent handleTouch_();
  HubUiEvent touchDevices_(int16_t x, int16_t y);
  HubUiEvent touchDetail_(int16_t x, int16_t y);
  HubUiEvent touchSensors_(int16_t x, int16_t y);
  HubUiEvent touchWorld_(int16_t x, int16_t y);

  // --- rendering ---
  void draw_();
  void drawHeader_();
  void drawTabs_();
  void drawDevices_();
  void drawDeviceCard_(uint8_t slot, int8_t devIdx);
  void drawDetail_();
  void drawSensors_();
  void drawSensorList_();
  void drawSensorDetail_();
  void drawSensorSpark_();
  void drawWorld_();
  void drawWorldCard_(int16_t x, int16_t y, int16_t w, int16_t h, const char *label,
                      const String &big, const String &sub, const String &extra,
                      bool valid, unsigned long ms, uint16_t accentColor);
  void drawEvents_();

  // --- model helpers ---
  int8_t findOrAddSensor_(const String &name);
  int8_t newestSensor_() const;
  int8_t activeSensor_() const;   // pinned, else newest
  bool sensorStale_(const Sensor &s) const;
  uint8_t visibleSensors_(int8_t *out, uint8_t max) const;  // newest first
  int8_t findDevice_(const String &id) const;
  uint8_t deviceOrder_(int8_t *out) const;  // used device indices, table order
  uint8_t deviceCount_() const;
  uint8_t sensorCount_() const;
  void markCommand_(int8_t devIdx, bool on);  // stamp the detail command trail

  Sensor sensors_[kMaxSensors];
  Device devices_[kMaxDevices];
  int8_t pinnedSensor_ = -1;
  int8_t selDevice_ = -1;   // device shown on the DETAIL screen

  String events_[kEventCap];
  unsigned long eventMs_[kEventCap];
  uint8_t eventHead_ = 0;
  uint8_t eventCount_ = 0;

  WorldFeeds world_;
  uint32_t sensorEvents_ = 0;

  bool ready_ = false;
  bool dirty_ = false;
  uint32_t lastDrawMs_ = 0;
#endif
};

#endif
