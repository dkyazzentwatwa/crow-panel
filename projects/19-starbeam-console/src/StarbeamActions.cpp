#include "StarbeamTypes.h"

// The canonical action table. `command` is the serial string the UART
// co-processor (stock starbeam_v2) understands; three items marked "(patch)"
// exist as Starbeam menu items but are not in stock terminal.cpp yet — they
// need a one-line addition to the co-proc's command table to be drivable.
const StarbeamActionInfo kStarbeamActions[ACT_COUNT] = {
    {ACT_NONE,          CAT_SETTINGS,  TGT_LOCAL,  "-",              "",              false},

    // --- Jammers (native nRF24 / CC1101 transmit) ---
    {ACT_BT_JAM,        CAT_JAMMERS,   TGT_NATIVE, "BT Jammer",      "bt_jam",        true},
    {ACT_DRONE_JAM,     CAT_JAMMERS,   TGT_NATIVE, "Drone Jammer",   "drone_jam",     true},
    {ACT_WIFI_JAM,      CAT_JAMMERS,   TGT_NATIVE, "WiFi Jammer",    "wifi_jam",      true},
    {ACT_CC1_JAM,       CAT_JAMMERS,   TGT_NATIVE, "CC1101 Jammer",  "cc1_jam",       true},

    // --- Scanners ---
    {ACT_NRF_SCAN,      CAT_SCANNERS,  TGT_NATIVE, "NRF Spectrum",   "nrf_scan",      false},
    {ACT_CC_SCAN,       CAT_SCANNERS,  TGT_NATIVE, "CC1101 Scan",    "cc_scan",       false},
    {ACT_GET_RSSI,      CAT_SCANNERS,  TGT_NATIVE, "CC1101 RSSI",    "get_rssi",      false},
    {ACT_WIFI_SCAN,     CAT_SCANNERS,  TGT_COPROC, "WiFi Scanner",   "wifi_scan",     false},
    {ACT_WIFI_HEATMAP,  CAT_SCANNERS,  TGT_COPROC, "WiFi Heatmap",   "wifi_heatmap",  false},
    {ACT_BLE_SCAN,      CAT_SCANNERS,  TGT_COPROC, "BLE Scanner",    "ble_scan",      false},
    {ACT_FLOCK_DETECTOR,CAT_SCANNERS,  TGT_COPROC, "Flock Detector", "flock_detect",  false},
    {ACT_PKT_MONITOR,   CAT_SCANNERS,  TGT_COPROC, "Pkt Monitor",    "pkt_monitor",   false},

    // --- Security (Wi-Fi/BT attacks on the co-processor) ---
    {ACT_DEAUTH_TARGET, CAT_SECURITY,  TGT_COPROC, "Deauth Target",  "deauth_target", true},
    {ACT_DEAUTH_ALL,    CAT_SECURITY,  TGT_COPROC, "Deauth All",     "deauth_all",    true},
    {ACT_BEACON_FLOOD,  CAT_SECURITY,  TGT_COPROC, "Beacon Flood",   "beacon_flood",  true},
    {ACT_PROBE_FLOOD,   CAT_SECURITY,  TGT_COPROC, "Probe Flood",    "probe_flood",   true},
    {ACT_PMKID,         CAT_SECURITY,  TGT_COPROC, "PMKID Capture",  "pmkid",         false},
    {ACT_CAPTIVE_PORTAL,CAT_SECURITY,  TGT_COPROC, "Captive Portal", "captive_portal",true},
    {ACT_WEB_ON,        CAT_SECURITY,  TGT_COPROC, "Web Server ON",  "web_on",        false},
    {ACT_WEB_OFF,       CAT_SECURITY,  TGT_COPROC, "Web Server OFF", "web_off",       false},
    {ACT_WEB_STATUS,    CAT_SECURITY,  TGT_COPROC, "Web Status",     "web_status",    false},

    // --- Recording (433 MHz capture / replay, native) ---
    {ACT_REC_RAW,       CAT_RECORDING, TGT_NATIVE, "Record Raw",     "rec_raw",       false},
    {ACT_PLAY_RAW,      CAT_RECORDING, TGT_NATIVE, "Replay Raw",     "play_raw",      true},
    {ACT_SHOW_RAW,      CAT_RECORDING, TGT_NATIVE, "Show Raw",       "show_raw",      false},
    {ACT_SHOW_BUFF,     CAT_RECORDING, TGT_NATIVE, "Show Buffer",    "show_buff",     false},
    {ACT_FLUSH_BUFF,    CAT_RECORDING, TGT_NATIVE, "Flush Buffer",   "flush_buff",    false},

    // --- Radios & test ---
    {ACT_TEST_NRF,      CAT_RADIOS,    TGT_NATIVE, "Test NRF (bus1)","test_nrf",      false},
    {ACT_TEST_HSPI,     CAT_RADIOS,    TGT_NATIVE, "Test NRF (bus2)","test_hspi",     false},
    {ACT_TEST_CC,       CAT_RADIOS,    TGT_NATIVE, "Test CC1101",    "test_cc",       false},
    {ACT_CC1_SINGLE,    CAT_RADIOS,    TGT_NATIVE, "CC1 Single Jam", "cc1_single",    true},
    {ACT_CC2_SINGLE,    CAT_RADIOS,    TGT_NATIVE, "CC2 Single Jam", "cc2_single",    true},
    {ACT_RESET_CC,      CAT_RADIOS,    TGT_NATIVE, "Reset CC1101",   "reset_cc",      false},
    {ACT_STOP_ALL,      CAT_RADIOS,    TGT_LOCAL,  "STOP ALL",       "stop_all",      false},

    // --- Frequency presets (CC1101, native config) ---
    {ACT_FREQ_43440,    CAT_SETTINGS,  TGT_NATIVE, "434.40 MHz",     "freq_43440",    false},
    {ACT_FREQ_43430,    CAT_SETTINGS,  TGT_NATIVE, "434.30 MHz",     "freq_43430",    false},
    {ACT_FREQ_43400,    CAT_SETTINGS,  TGT_NATIVE, "434.00 MHz",     "freq_43400",    false},
    {ACT_FREQ_43390,    CAT_SETTINGS,  TGT_NATIVE, "433.90 MHz",     "freq_43390",    false},

    // --- Settings ---
    {ACT_SETTINGS,      CAT_SETTINGS,  TGT_LOCAL,  "Settings",       "settings",      false},
    {ACT_HELP,          CAT_SETTINGS,  TGT_LOCAL,  "Help",           "help",          false},
};

const StarbeamActionInfo &starbeamAction(StarbeamAction a) {
  if (a >= ACT_COUNT) a = ACT_NONE;
  return kStarbeamActions[a];
}

const char *starbeamCategoryLabel(StarbeamCategory c) {
  switch (c) {
    case CAT_JAMMERS:   return "JAMMERS";
    case CAT_SCANNERS:  return "SCANNERS";
    case CAT_SECURITY:  return "WIFI / BT SECURITY";
    case CAT_RECORDING: return "RECORDING";
    case CAT_RADIOS:    return "RADIOS & TEST";
    case CAT_SETTINGS:  return "SETTINGS";
    default:            return "";
  }
}
