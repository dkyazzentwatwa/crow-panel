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

// Bluetooth-LE HID output via the onboard ESP32-C6 (NimBLE host on the P4,
// esp_hosted VHCI to the C6 radio). Off by default: it adds ~425 KB and needs
// the C6. Does NOT require USB-OTG (compiles under hwcdc too), but the full
// dual-mode deliverable is USBMode=default + USE_USB_HID=1 + USE_BLE_HID=1.
#ifndef USE_BLE_HID
#define USE_BLE_HID 0
#endif

// Name shown in the host's Bluetooth device list.
#ifndef CYPHER_KEYS_BLE_NAME
#define CYPHER_KEYS_BLE_NAME "Cypher Keys"
#endif

// Synthesized mechanical-keyboard switch sounds for the on-screen keyboard,
// played out of the NS4168 I2S amp + speaker. Off by default: the amp path is
// shared hardware and a silent build must stay the suite's default.
//
// The I2S bring-up is transcribed from project 09 (Cypher Tune MPC), whose
// AudioEngine is hardware-proven audible on this board: IDF <driver/i2s_std.h>
// (never the Arduino ESP_I2S wrapper), silence written before the amp enable,
// and the amp control level taken from HardwareProfile (it is active-LOW).
//
// Sounds are synthesized into small PCM buffers at boot (~16 KB total); there
// are no sample files and no SD dependency. See src/KeyAudio.{h,cpp}.
#ifndef USE_CYPHER_KEYS_AUDIO
#define USE_CYPHER_KEYS_AUDIO 0
#endif

// I2S sample rate for the click engine. 22050 Hz is plenty for 10-30 ms
// transients (Nyquist ~11 kHz) and halves both the buffer cost and the render
// task's wakeup rate versus 44100.
#ifndef CYPHER_KEYS_AUDIO_SAMPLE_RATE
#define CYPHER_KEYS_AUDIO_SAMPLE_RATE 22050
#endif

// Optional SD-card sound packs: REAL recorded switch samples, loaded off the
// card and played instead of the synthesized profiles. Off by default, and the
// synthesized profiles stay the always-available fallback - a card is a nice
// upgrade, never a dependency. Needs USE_CYPHER_KEYS_AUDIO=1 to be audible.
//
// A pack is a folder of 16-bit PCM mono WAVs at CYPHER_KEYS_AUDIO_SAMPLE_RATE
// (so nothing is ever resampled on the keypress path):
//
//   <CYPHER_KEYS_SOUNDS_DIR>/<pack>/press/GENERIC_R0..R4.wav
//   <CYPHER_KEYS_SOUNDS_DIR>/<pack>/press/{BACKSPACE,ENTER,SPACE}.wav
//   <CYPHER_KEYS_SOUNDS_DIR>/<pack>/release/GENERIC.wav
//   <CYPHER_KEYS_SOUNDS_DIR>/<pack>/release/{BACKSPACE,ENTER,SPACE}.wav
//
// Every file except press/GENERIC_R0.wav is optional; see src/KeySoundPacks.h
// for the resolution order. scripts/convert-key-sounds.sh builds this layout.
// No audio is vendored in the repo - see TECHNICAL.md.
#ifndef USE_CYPHER_KEYS_SD
#define USE_CYPHER_KEYS_SD 0
#endif

// Where the pack folders live on the card.
#ifndef CYPHER_KEYS_SOUNDS_DIR
#define CYPHER_KEYS_SOUNDS_DIR "/cypher-keys/sounds"
#endif

// SD_MMC bus width. 1-bit is the conservative bring-up the rest of the suite
// defaults to (projects 02/08/09/20); set to 0 for the faster 4-bit bus once the
// card and wiring are known good.
#ifndef CYPHER_KEYS_SDMMC_1BIT
#define CYPHER_KEYS_SDMMC_1BIT 1
#endif

// Click volume, 0-100 percent. Persisted in NVS under "sndvol"; this is only
// the value used the first time, before anything is stored.
#ifndef CYPHER_KEYS_AUDIO_VOLUME
#define CYPHER_KEYS_AUDIO_VOLUME 70
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

// Panel backlight the deck boots at, 0-255. Persisted in NVS under "bright";
// this is only the value used before anything is stored. The settings screen
// floors it at HidDeck::kMinBrightness, because a keyboard you cannot see is
// indistinguishable from a crash.
#ifndef CYPHER_KEYS_BRIGHTNESS
#define CYPHER_KEYS_BRIGHTNESS 255
#endif

// Idle dimming: with no touch for this long the backlight ramps down to
// CYPHER_KEYS_IDLE_DIM_LEVEL, and the next tap restores it instantly (that tap
// is consumed - it wakes the panel and does NOT type). Only active while the
// Idle-dim setting is on (persisted in NVS under "idledim").
//
// 60 s is deliberately shorter than an ambient dashboard's: a keyboard sits
// untouched between bursts of typing, and the wake tap costs nothing.
#ifndef CYPHER_KEYS_IDLE_DIM_MS
#define CYPHER_KEYS_IDLE_DIM_MS 60000
#endif

// How dark idle dimming goes, 0-255. Low enough to read as "asleep", not 0:
// the keys must stay faintly visible so it is obvious where to tap to wake it.
#ifndef CYPHER_KEYS_IDLE_DIM_LEVEL
#define CYPHER_KEYS_IDLE_DIM_LEVEL 24
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

// Multi-contact matching: the GT911 keeps a track id per finger, but on some
// panels those ids churn between samples. An unmatched incoming point that
// lands within this many mapped pixels of an unmatched live contact is treated
// as the same finger continuing, not as a new press.
#ifndef CYPHER_KEYS_TOUCH_MATCH_RADIUS
#define CYPHER_KEYS_TOUCH_MATCH_RADIUS 48
#endif

// Hold-to-repeat for Backspace and the arrow keys: how long a finger must rest
// on the key before the first repeat, and the interval between repeats after
// that. 400/60 ms is close to a Mac's own default key-repeat feel. Each repeat
// is a full down/up pair through HidBackend (which holds a key for 24 ms), so
// the interval must stay comfortably above that hold window.
#ifndef CYPHER_KEYS_KEY_REPEAT_DELAY_MS
#define CYPHER_KEYS_KEY_REPEAT_DELAY_MS 400
#endif
#ifndef CYPHER_KEYS_KEY_REPEAT_MS
#define CYPHER_KEYS_KEY_REPEAT_MS 60
#endif

#endif
