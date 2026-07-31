#ifndef CROW_PANEL_APP_CONFIG_H
#define CROW_PANEL_APP_CONFIG_H

#define CROWPANEL_P4_7IN_V1_0 100
#define CROWPANEL_P4_7IN_V1_1 110
#define CROWPANEL_P4_7IN_V1_2 120

#ifndef MOCK_MODE
#define MOCK_MODE 1
#endif

#ifndef USE_DISPLAY
#define USE_DISPLAY 0
#endif

#ifndef USE_WIFI
#define USE_WIFI 0
#endif

#ifndef USE_LORA_DRIVER
#define USE_LORA_DRIVER 0
#endif

// FieldOps ESP-NOW transport: the panel reads sensor/presence frames over
// UART from an ESP-NOW<->UART bridge (a plain ESP32). The ESP32-P4 cannot be
// an ESP-NOW peer directly (WiFi is remote on the C6), so this flag only
// enables the UART reader on the panel - the radio lives on the bridge.
#ifndef USE_ESPNOW
#define USE_ESPNOW 0
#endif

#ifndef USE_CAMERA_DRIVER
#define USE_CAMERA_DRIVER 0
#endif

#ifndef USE_PN532_DRIVER
#define USE_PN532_DRIVER 0
#endif

#ifndef USE_MFRC522_DRIVER
#define USE_MFRC522_DRIVER 0
#endif

#ifndef USE_AUDIO
#define USE_AUDIO 0
#endif

#ifndef USE_WIFI_SCAN
#define USE_WIFI_SCAN 0
#endif

#ifndef USE_FLOCK_C6_WITNESS
#define USE_FLOCK_C6_WITNESS 0
#endif

#ifndef USE_RF_LAB_C6_WIFI
#define USE_RF_LAB_C6_WIFI 0
#endif

#ifndef USE_BLE_UART_BRIDGE
#define USE_BLE_UART_BRIDGE 0
#endif

#ifndef USE_QR_PERSISTENCE
#define USE_QR_PERSISTENCE 0
#endif

// Shared CrowHid stack (shared/CrowPanelShared/CrowHid*): USB + BLE HID output.
// Used by project 21 (Cypher Keys) and project 05 (CypherDrive). Defaults off so
// shared TUs and the flag matrix build the mock path. USE_USB_HID=1 needs an
// USBMode=default FQBN to go live (else it falls back to MOCK with a #warning);
// USE_BLE_HID=1 drives BLE HID over the onboard C6 (NimBLE) and builds under the
// default hwcdc FQBN too.
#ifndef USE_USB_HID
#define USE_USB_HID 0
#endif

#ifndef USE_BLE_HID
#define USE_BLE_HID 0
#endif

// Project 05 (CypherDrive) active field-tool paths. USE_WIFI_ACTIVE turns the
// Wi-Fi scanner active (probe scan) and unlocks join + client tools through the
// hosted C6 (also needs USE_WIFI so the shared CrowNetworkClient join/HTTP path
// compiles). USE_BLE_C6 drives on-panel NimBLE central scan/GATT over the C6.
#ifndef USE_WIFI_ACTIVE
#define USE_WIFI_ACTIVE 0
#endif

#ifndef USE_BLE_C6
#define USE_BLE_C6 0
#endif

#ifndef USE_SD_HIGHSCORES
#define USE_SD_HIGHSCORES 0
#endif

#ifndef USE_RF_UART_BRIDGE
#define USE_RF_UART_BRIDGE 0
#endif

#ifndef USE_GPS_DRIVER
#define USE_GPS_DRIVER 0
#endif

#ifndef USE_SD_WIGLE_LOG
#define USE_SD_WIGLE_LOG 0
#endif

#ifndef USE_SD_POKEDEX
#define USE_SD_POKEDEX 0
#endif

#ifndef USE_POKEDEX_SPRITES
#define USE_POKEDEX_SPRITES 0
#endif

#ifndef USE_POKEDEX_AUDIO
#define USE_POKEDEX_AUDIO 0
#endif

// Project 19 (Starbeam Console): native nRF24/CC1101 stack + UART co-processor
// link + transmit-arming gate. Off by default so shared TUs and the flag matrix
// build without the RF24 / SmartRC-CC1101 libraries or a live co-processor.
#ifndef USE_STARBEAM_RADIOS
#define USE_STARBEAM_RADIOS 0
#endif

#ifndef USE_STARBEAM_COPROC
#define USE_STARBEAM_COPROC 0
#endif

#ifndef STARBEAM_TX_CONFIRMED
#define STARBEAM_TX_CONFIRMED 0
#endif

// GT911 touch mapping for the shared CrowTouch helper. Defaults assume the
// panel reports 1024x600-aligned coordinates already; override per project
// (or per board revision) after a `touch` diagnostic run.
#ifndef CROW_TOUCH_MIN_X
#define CROW_TOUCH_MIN_X 0
#endif
#ifndef CROW_TOUCH_MAX_X
#define CROW_TOUCH_MAX_X 1023
#endif
#ifndef CROW_TOUCH_MIN_Y
#define CROW_TOUCH_MIN_Y 0
#endif
#ifndef CROW_TOUCH_MAX_Y
#define CROW_TOUCH_MAX_Y 599
#endif
#ifndef CROW_TOUCH_SWAP_XY
#define CROW_TOUCH_SWAP_XY 0
#endif
#ifndef CROW_TOUCH_INVERT_X
#define CROW_TOUCH_INVERT_X 0
#endif
#ifndef CROW_TOUCH_INVERT_Y
#define CROW_TOUCH_INVERT_Y 0
#endif

// A release is only accepted after the panel reads no contact for this long,
// which collapses GT911 flicker into one clean press/release pair. Without it
// a single tap can fire several times and a drag jumps around.
#ifndef CROW_TOUCH_RELEASE_DEBOUNCE_MS
#define CROW_TOUCH_RELEASE_DEBOUNCE_MS 30
#endif

// Size of the SerialCommandRouter command table. Serial is a first-class
// control surface on every project here, not a demo affordance, so this is
// sized to never be the thing that stops you adding a command: 128 entries
// cost 128 * 12 = 1.5 KB of static RAM per sketch (one router per sketch) on a
// chip with 768 KB of SRAM.
//
// This was 12 from commit 0504e01 until 2026-07-31, and SerialCommandRouter::on()
// drops registrations past the limit while returning a value no sketch checks.
// Eight projects had silently dead commands as a result - project 09 lost its
// entire Serial transport (`play`/`stop`/`record`/`engine`).
//
// DO NOT override this in a project's ProjectConfig.h. It sizes an array
// member inside SerialCommandRouter, and per the three-layer flag rule the
// shared .cpp files never see ProjectConfig.h - so a project-local override
// gives the sketch and the library different object layouts. That is memory
// corruption, not a compile error. If a project ever needs more, either raise
// the number here for everyone, or pass -DCROW_SERIAL_MAX_COMMANDS via
// compiler.cpp.extra_flags so every translation unit agrees.
#ifndef CROW_SERIAL_MAX_COMMANDS
#define CROW_SERIAL_MAX_COMMANDS 128
#endif

#ifndef CROWPANEL_HARDWARE_PROFILE
#define CROWPANEL_HARDWARE_PROFILE CROWPANEL_P4_7IN_V1_2
#endif

#endif
