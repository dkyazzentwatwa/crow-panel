#ifndef CYPHERDRIVE_PROJECT_CONFIG_H
#define CYPHERDRIVE_PROJECT_CONFIG_H

// Project 05 - CypherDrive Active Field Tool.
//
// A touch-first ACTIVE wireless + HID field tool built on the onboard ESP32-C6.
// Mock-first: every flag defaults off so the whole tool is exercisable with no
// radio and stays green in the shared flag matrix. Enable one path at a time
// with compiler -D flags when proving hardware.
//
// Capability flags (defaults also live in shared AppConfig.h):
//   USE_WIFI_ACTIVE : active probe scan + join + client tools via the C6.
//   USE_BLE_C6      : on-panel NimBLE central scan/GATT via the C6 (at-risk;
//                     C6 hosted NimBLE central is unproven on hardware).
//   USE_USB_HID     : native USB HID output (needs an USBMode=default FQBN).
//   USE_BLE_HID     : BLE HID output via the C6 (builds under hwcdc too).
//
// Deliberately NOT built: Wi-Fi deauth/jamming, evil-twin/captive-portal
// credential capture, or unattended BadUSB autorun. Captive handling is
// detection only; HID is operator-driven.

#ifndef USE_WIFI_ACTIVE
#define USE_WIFI_ACTIVE 0
#endif

#ifndef USE_BLE_C6
#define USE_BLE_C6 0
#endif

#ifndef USE_USB_HID
#define USE_USB_HID 0
#endif

#ifndef USE_BLE_HID
#define USE_BLE_HID 0
#endif

// Per-channel dwell for the active scan, in ms.
#ifndef CYPHERDRIVE_WIFI_SCAN_MS_PER_CHANNEL
#define CYPHERDRIVE_WIFI_SCAN_MS_PER_CHANNEL 120
#endif

// SD_MMC export + SD-loaded HID payloads. Default off: a card-free build
// compiles and every SD call is a logged no-op. 1-bit bus is the conservative
// bring-up the rest of the suite defaults to.
#ifndef USE_CYPHERDRIVE_SD
#define USE_CYPHERDRIVE_SD 0
#endif

#ifndef CYPHERDRIVE_SDMMC_1BIT
#define CYPHERDRIVE_SDMMC_1BIT 1
#endif

// Join credentials for secured networks come from a gitignored WiFiSecrets.h
// (edit config/WiFiSecrets.example.h -> WiFiSecrets.h). When the selected SSID
// matches CYPHERDRIVE_JOIN_SSID the tool uses CYPHERDRIVE_JOIN_PASS; open
// networks join with no key; any other secured network needs its key added here
// first. Never commit real credentials.
#if __has_include("WiFiSecrets.h")
#include "WiFiSecrets.h"
#endif

#ifndef CYPHERDRIVE_JOIN_SSID
#define CYPHERDRIVE_JOIN_SSID ""
#endif

#ifndef CYPHERDRIVE_JOIN_PASS
#define CYPHERDRIVE_JOIN_PASS ""
#endif

// Default target for the TCP port-scan tool when none is entered (empty falls
// back to the current gateway at runtime).
#ifndef CYPHERDRIVE_PORTSCAN_TARGET
#define CYPHERDRIVE_PORTSCAN_TARGET ""
#endif

#include <AppConfig.h>

#endif
