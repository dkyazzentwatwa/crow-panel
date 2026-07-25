#ifndef STARBEAM_CONSOLE_TYPES_H
#define STARBEAM_CONSOLE_TYPES_H

#include <Arduino.h>

// ============================================================================
// Action model — the 38 Starbeam menu items, grouped into touch categories.
// Order is independent of the OLED menu; the UI drives everything off this.
// ============================================================================

enum StarbeamCategory : uint8_t {
  CAT_JAMMERS = 0,   // nRF24 + CC1101 transmit
  CAT_SCANNERS,      // spectrum / RSSI / network discovery
  CAT_SECURITY,      // Wi-Fi/BT attacks (co-processor)
  CAT_RECORDING,     // 433 MHz capture / replay
  CAT_RADIOS,        // register tests / reset / stop
  CAT_SETTINGS,      // config / help
  CAT_COUNT
};

enum StarbeamAction : uint8_t {
  ACT_NONE = 0,
  // Jammers
  ACT_BT_JAM, ACT_DRONE_JAM, ACT_WIFI_JAM, ACT_CC1_JAM,
  // Scanners
  ACT_NRF_SCAN, ACT_CC_SCAN, ACT_GET_RSSI,
  ACT_WIFI_SCAN, ACT_WIFI_HEATMAP, ACT_BLE_SCAN, ACT_FLOCK_DETECTOR, ACT_PKT_MONITOR,
  // Security (co-proc)
  ACT_DEAUTH_TARGET, ACT_DEAUTH_ALL, ACT_BEACON_FLOOD, ACT_PROBE_FLOOD, ACT_PMKID,
  ACT_CAPTIVE_PORTAL, ACT_WEB_ON, ACT_WEB_OFF, ACT_WEB_STATUS,
  // Recording
  ACT_REC_RAW, ACT_PLAY_RAW, ACT_SHOW_RAW, ACT_SHOW_BUFF, ACT_FLUSH_BUFF,
  // Radios & test
  ACT_TEST_NRF, ACT_TEST_HSPI, ACT_TEST_CC, ACT_CC1_SINGLE, ACT_CC2_SINGLE,
  ACT_RESET_CC, ACT_STOP_ALL,
  // Frequency presets (CC1101)
  ACT_FREQ_43440, ACT_FREQ_43430, ACT_FREQ_43400, ACT_FREQ_43390,
  // Settings
  ACT_SETTINGS, ACT_HELP,
  ACT_COUNT
};

// Where an action executes.
enum StarbeamTarget : uint8_t {
  TGT_NATIVE = 0,   // runs on the P4's own nRF24/CC1101 stack
  TGT_COPROC,       // forwarded over UART to the ESP32 dev module
  TGT_LOCAL         // panel-only (settings, help, stop)
};

struct StarbeamActionInfo {
  StarbeamAction action;
  StarbeamCategory category;
  StarbeamTarget target;
  const char *label;       // touch-tile label
  const char *command;     // co-proc serial command (TGT_COPROC only), else ""
  bool requiresTx;         // gated behind STARBEAM_TX_CONFIRMED
};

// Full action table (defined in StarbeamActions.cpp).
extern const StarbeamActionInfo kStarbeamActions[ACT_COUNT];
const StarbeamActionInfo &starbeamAction(StarbeamAction a);
const char *starbeamCategoryLabel(StarbeamCategory c);

// ============================================================================
// Live console state, rendered by the UI and updated by the engines.
// ============================================================================

struct RadioSlot {
  bool present = false;    // register-probe detected
  uint8_t reg = 0xFF;      // nRF STATUS or CC1101 PARTNUM
};

struct CoProcSnapshot {
  bool linked = false;         // UART peer answered a ping
  uint32_t frames = 0;         // deauth/beacon/probe frame counter
  uint32_t clients = 0;        // deauth clients hit
  uint32_t captures = 0;       // PMKID / portal captures
  uint16_t wifiNetworks = 0;   // scan count
  int16_t wifiStrongest = -127;
  uint16_t bleDevices = 0;
  uint8_t wifiChannels[14] = {0};  // heatmap: per-channel utilisation 0..100
  char status[40] = "offline";
  uint32_t lastRxMs = 0;
};

struct StarbeamState {
  // radios
  RadioSlot nrf[5];
  RadioSlot cc[2];
  uint8_t nrfPresentCount = 0;
  uint8_t ccPresentCount = 0;

  // active operation
  StarbeamAction active = ACT_NONE;
  bool running = false;
  bool txConfirmed = false;      // mirrors STARBEAM_TX_CONFIRMED

  // native telemetry
  uint8_t spectrum[128] = {0};   // nRF24 per-channel signal 0..10
  uint8_t spectrumPeak = 0;
  float ccFreqMhz = 433.92f;
  float ccRssiDbm = -127.0f;
  int ccLqi = 0;
  uint32_t jamCycles = 0;

  // recording
  uint16_t recBytes = 0;
  bool recBufferValid = false;

  // co-processor
  CoProcSnapshot co;

  // banner / status line
  char banner[64] = "booting";
};

#endif  // STARBEAM_CONSOLE_TYPES_H
