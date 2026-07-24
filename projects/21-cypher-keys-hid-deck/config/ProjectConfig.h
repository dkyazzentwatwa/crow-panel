#ifndef CYPHER_KEYS_HID_DECK_PROJECT_CONFIG_H
#define CYPHER_KEYS_HID_DECK_PROJECT_CONFIG_H

#include <AppConfig.h>

// Project 21 - Cypher Keys HID Deck.
//
// Turns the CrowPanel into a native USB device for a host (a Mac): a touch
// keyboard, a switchable macro pad, and a trackpad, all sent over USB HID from
// the ESP32-P4's USB-OTG (TinyUSB) path.
//
// Honest, mock-first gating (matches the rest of the suite):
//
//  - USE_USB_HID=0 (default): MOCK. No USB HID device is created; every
//    intended report is printed to Serial and the event log. This build
//    compiles under the suite's default USBMode=hwcdc FQBN and stays green in
//    the shared flag matrix.
//
//  - USE_USB_HID=1: LIVE. Real TinyUSB keyboard + consumer-control + mouse.
//    This REQUIRES a USB-OTG build, i.e. the FQBN must use USBMode=default
//    (ARDUINO_USB_MODE==0). Under USBMode=hwcdc the P4's native USB is CDC/JTAG
//    only, so the backend transparently falls back to MOCK and emits a
//    compile-time #warning (see HidBackend.h). Nothing here overrides the
//    platform-owned build.extra_flags.esp32p4 USB defines.
#ifndef USE_USB_HID
#define USE_USB_HID 0
#endif

// Number of macro tiles per preset (grid rows x cols) and the tab bar width.
#ifndef CYPHER_KEYS_MACRO_SLOTS
#define CYPHER_KEYS_MACRO_SLOTS 12  // 3 rows x 4 columns
#endif
#ifndef CYPHER_KEYS_MACRO_COLS
#define CYPHER_KEYS_MACRO_COLS 4
#endif
#ifndef CYPHER_KEYS_MAX_PRESETS
#define CYPHER_KEYS_MAX_PRESETS 6
#endif

// Longest canned text snippet a Text-kind macro slot may type.
#ifndef CYPHER_KEYS_MACRO_TEXT_MAX
#define CYPHER_KEYS_MACRO_TEXT_MAX 160
#endif

// NVS namespace/key for remembering the last-used preset across reboot.
#ifndef CYPHER_KEYS_NVS_NAMESPACE
#define CYPHER_KEYS_NVS_NAMESPACE "cypherkeys"
#endif
#ifndef CYPHER_KEYS_NVS_PRESET_KEY
#define CYPHER_KEYS_NVS_PRESET_KEY "preset"
#endif

// Trackpad tuning: pointer speed (touch delta -> mouse counts) and the largest
// per-report relative step (USB HID relative mouse is int8_t).
#ifndef CYPHER_KEYS_TRACKPAD_GAIN_NUM
#define CYPHER_KEYS_TRACKPAD_GAIN_NUM 3
#endif
#ifndef CYPHER_KEYS_TRACKPAD_GAIN_DEN
#define CYPHER_KEYS_TRACKPAD_GAIN_DEN 2
#endif
#ifndef CYPHER_KEYS_TRACKPAD_MAX_STEP
#define CYPHER_KEYS_TRACKPAD_MAX_STEP 100
#endif

// GT911 touch calibration (copied from Cypher Desk defaults; override per board
// after a `touch` diagnostic run).
#ifndef CYPHER_KEYS_TOUCH_MIN_X
#define CYPHER_KEYS_TOUCH_MIN_X 0
#endif
#ifndef CYPHER_KEYS_TOUCH_MAX_X
#define CYPHER_KEYS_TOUCH_MAX_X 1023
#endif
#ifndef CYPHER_KEYS_TOUCH_MIN_Y
#define CYPHER_KEYS_TOUCH_MIN_Y 0
#endif
#ifndef CYPHER_KEYS_TOUCH_MAX_Y
#define CYPHER_KEYS_TOUCH_MAX_Y 599
#endif
#ifndef CYPHER_KEYS_TOUCH_SWAP_XY
#define CYPHER_KEYS_TOUCH_SWAP_XY 0
#endif
#ifndef CYPHER_KEYS_TOUCH_INVERT_X
#define CYPHER_KEYS_TOUCH_INVERT_X 0
#endif
#ifndef CYPHER_KEYS_TOUCH_INVERT_Y
#define CYPHER_KEYS_TOUCH_INVERT_Y 0
#endif

// How often to actually read the GT911. Its point register is cleared on read,
// so polling FASTER than the panel refreshes returns empty "no contact" frames -
// which makes the trackpad stutter. ~16 ms (about 60 Hz) stays at or below the
// panel's refresh so every read returns a fresh point, giving smooth tracking.
// This is decoupled from the main loop so rendering/HID can still run fast.
#ifndef CYPHER_KEYS_TOUCH_POLL_MS
#define CYPHER_KEYS_TOUCH_POLL_MS 16
#endif

// The GT911 briefly drops/re-reports a contact ("flicker") during a real touch.
// A release is only accepted after the panel has read no contact for this long,
// which collapses flicker into one clean press/release. Without it, one tap can
// emit several keys and a drag jumps around. ~30 ms bridges a few dropped 8 ms
// frames while still allowing very fast typing.
#ifndef CYPHER_KEYS_TOUCH_RELEASE_DEBOUNCE_MS
#define CYPHER_KEYS_TOUCH_RELEASE_DEBOUNCE_MS 30
#endif

#endif
