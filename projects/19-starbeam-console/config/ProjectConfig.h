#ifndef STARBEAM_CONSOLE_PROJECT_CONFIG_H
#define STARBEAM_CONSOLE_PROJECT_CONFIG_H

// Project 19: Starbeam Console.
// A full 1:1 port of project-starbeam (starbeam_v2) onto the CrowPanel
// Advanced ESP32-P4. The P4 drives all seven radios natively over one shared
// SPI bus (5x nRF24L01+, 2x CC1101). The ESP32-native Wi-Fi/BLE half of
// Starbeam (deauth, beacon/probe flood, PMKID, packet monitor, Wi-Fi/BLE scan,
// captive portal, web server, flock detector) runs on a UART-attached ESP32
// dev module flashed with stock starbeam_v2; this panel drives it over Serial1
// and renders the telemetry. See README.md.

// Per-machine wiring override (gitignored). Falls back to the verified map
// below when absent.
#if __has_include("Pins.h")
#include "Pins.h"
#endif

// Local authorization gate. Copy LabProfile.example.h -> LabProfile.h (which is
// gitignored) and set STARBEAM_TX_CONFIRMED 1 only after confirming that the
// connected radios, antennas, and frequencies are yours or explicitly
// authorized for this lab. Without it, transmit/jam actions stay disarmed.
#if __has_include("LabProfile.h")
#include "LabProfile.h"
#endif

// ============================================================================
// Feature flags (all #ifndef-guarded so -D defines and AppConfig.h win)
// ============================================================================

// Compile and drive the native nRF24 + CC1101 stack. Off by default so the
// shared flag matrix stays green without the RF24 / SmartRC-CC1101 libraries;
// the real build passes -DUSE_STARBEAM_RADIOS=1.
#ifndef USE_STARBEAM_RADIOS
#define USE_STARBEAM_RADIOS 0
#endif

// Compile and drive the UART link to the ESP32 dev module (Wi-Fi/BLE brain).
#ifndef USE_STARBEAM_COPROC
#define USE_STARBEAM_COPROC 0
#endif

// Master arming gate for any RF transmit (all jammers + CC1101 SendData) and
// for forwarding attack commands to the co-processor. Set to 1 only from a
// local, gitignored LabProfile.h. Everything still compiles and the UI still
// renders when 0 — transmit paths are simply refused.
#ifndef STARBEAM_TX_CONFIRMED
#define STARBEAM_TX_CONFIRMED 0
#endif

#ifndef STARBEAM_PROFILE_NAME
#define STARBEAM_PROFILE_NAME "UNCONFIRMED"
#endif

// ============================================================================
// Radio bus timing
// ============================================================================

#ifndef STARBEAM_NRF_SPI_HZ
#define STARBEAM_NRF_SPI_HZ 10000000UL
#endif

#ifndef STARBEAM_SPECTRUM_INTERVAL_MS
#define STARBEAM_SPECTRUM_INTERVAL_MS 40UL
#endif

// CC1101 default frequency preset (MHz*100 to stay integer in macros).
#ifndef STARBEAM_CC1101_MHZ_X100
#define STARBEAM_CC1101_MHZ_X100 43392
#endif

// ============================================================================
// Co-processor UART link (P4 <-> ESP32 dev module running stock starbeam_v2)
// ============================================================================

#ifndef STARBEAM_COPROC_BAUD
#define STARBEAM_COPROC_BAUD 115200UL
#endif

#ifndef STARBEAM_COPROC_UART_NUM
#define STARBEAM_COPROC_UART_NUM 1        // use Serial1
#endif

// ============================================================================
// P4 GPIO map (CrowPanel external + wireless headers). Keep pins here, never
// scattered in src/. Verify strapping on IO2 / IO45 / IO46 against the ESP32-P4
// datasheet before soldering (CSN idles high = the strap risk).
// ============================================================================

// Shared radio SPI (all 5 nRF24 + 2 CC1101; unique CS/CE per radio)
#ifndef STARBEAM_SPI_SCK
#define STARBEAM_SPI_SCK 6
#endif
#ifndef STARBEAM_SPI_MOSI
#define STARBEAM_SPI_MOSI 7
#endif
#ifndef STARBEAM_SPI_MISO
#define STARBEAM_SPI_MISO 8
#endif

// nRF24 R0..R4 chip-selects
#ifndef STARBEAM_NRF0_CSN
#define STARBEAM_NRF0_CSN 9
#endif
#ifndef STARBEAM_NRF1_CSN
#define STARBEAM_NRF1_CSN 10
#endif
#ifndef STARBEAM_NRF2_CSN
#define STARBEAM_NRF2_CSN 53
#endif
#ifndef STARBEAM_NRF3_CSN
#define STARBEAM_NRF3_CSN 54
#endif
#ifndef STARBEAM_NRF4_CSN
#define STARBEAM_NRF4_CSN 45
#endif

// nRF24 R0..R4 chip-enables
#ifndef STARBEAM_NRF0_CE
#define STARBEAM_NRF0_CE 46
#endif
#ifndef STARBEAM_NRF1_CE
#define STARBEAM_NRF1_CE 2
#endif
#ifndef STARBEAM_NRF2_CE
#define STARBEAM_NRF2_CE 3
#endif
#ifndef STARBEAM_NRF3_CE
#define STARBEAM_NRF3_CE 4
#endif
#ifndef STARBEAM_NRF4_CE
#define STARBEAM_NRF4_CE 5
#endif

// CC1101 #1 / #2 (share the SPI bus; GDO2 left unconnected)
#ifndef STARBEAM_CC0_CS
#define STARBEAM_CC0_CS 49
#endif
#ifndef STARBEAM_CC0_GDO0
#define STARBEAM_CC0_GDO0 27
#endif
#ifndef STARBEAM_CC1_CS
#define STARBEAM_CC1_CS 28
#endif
#ifndef STARBEAM_CC1_GDO0
#define STARBEAM_CC1_GDO0 25
#endif

// Co-processor UART (P4 TX -> dev RX, P4 RX <- dev TX, common GND)
#ifndef STARBEAM_COPROC_TX
#define STARBEAM_COPROC_TX 51
#endif
#ifndef STARBEAM_COPROC_RX
#define STARBEAM_COPROC_RX 50
#endif

// Optional freeze button / status LED
#ifndef STARBEAM_FREEZE_PIN
#define STARBEAM_FREEZE_PIN 52
#endif

#include <AppConfig.h>

#endif  // STARBEAM_CONSOLE_PROJECT_CONFIG_H
