#ifndef CYPHER_KEYS_HID_TYPES_H
#define CYPHER_KEYS_HID_TYPES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Modifier bitmask, host-agnostic. HidBackend translates these to the Arduino
// USB keyboard key codes (KEY_LEFT_GUI etc). On a Mac, "GUI" is Command.
enum HidMod : uint8_t {
  kModNone = 0,
  kModCmd = 1 << 0,   // Command (KEY_LEFT_GUI)
  kModShift = 1 << 1, // Shift
  kModOpt = 1 << 2,   // Option/Alt
  kModCtrl = 1 << 3,  // Control
};

// A "main key" is one uint8_t. Values < 0x80 are printable ASCII; values
// >= 0x80 are the Arduino USB special-key constants (KEY_RETURN=0xB0,
// KEY_TAB=0xB3, KEY_LEFT_ARROW=0xD8, ...). USBHIDKeyboard::press() accepts
// both, so a single byte covers every key we emit.

// Portable aliases for the special keys and consumer usages the deck emits.
// The numeric values match the Arduino USB library (USBHIDKeyboard.h /
// USBHIDConsumerControl.h) so HidBackend can pass them straight to press()/
// send(); duplicating them here keeps config/Macros.h free of any USB include
// (those headers only exist in a USB-OTG build).
static const uint8_t kKeyReturn = 0xB0;
static const uint8_t kKeyEsc = 0xB1;
static const uint8_t kKeyBackspace = 0xB2;
static const uint8_t kKeyTab = 0xB3;
static const uint8_t kKeyRightArrow = 0xD7;
static const uint8_t kKeyLeftArrow = 0xD8;
static const uint8_t kKeyDownArrow = 0xD9;
static const uint8_t kKeyUpArrow = 0xDA;
static const uint8_t kKeyF1 = 0xC2;
static const uint8_t kKeyF2 = 0xC3;
static const uint8_t kKeyF3 = 0xC4;
static const uint8_t kKeyF4 = 0xC5;
static const uint8_t kKeyF5 = 0xC6;  // macOS dictation/mic key on many keyboards
static const uint8_t kKeyF6 = 0xC7;
static const uint8_t kKeyF7 = 0xC8;
static const uint8_t kKeyF8 = 0xC9;
static const uint8_t kKeyF9 = 0xCA;
static const uint8_t kKeyF10 = 0xCB;
static const uint8_t kKeyF11 = 0xCC;
static const uint8_t kKeyF12 = 0xCD;

static const uint16_t kCcPlayPause = 0x00CD;
static const uint16_t kCcMute = 0x00E2;
static const uint16_t kCcVolumeUp = 0x00E9;
static const uint16_t kCcVolumeDown = 0x00EA;
static const uint16_t kCcBrightnessUp = 0x006F;
static const uint16_t kCcBrightnessDown = 0x0070;

// What a macro slot does when fired.
enum MacroKind : uint8_t {
  kMacroNone = 0,   // empty slot
  kMacroCombo,      // modifiers + one key (e.g. Cmd+C)
  kMacroConsumer,   // a consumer-control usage (volume, play/pause, ...)
  kMacroText,       // types a canned string
};

struct MacroSlot {
  const char *label;  // shown on the tile
  MacroKind kind;
  uint8_t mods;       // kMacroCombo: HidMod bitmask
  uint8_t key;        // kMacroCombo: ASCII or KEY_* constant
  uint16_t usage;     // kMacroConsumer: CONSUMER_CONTROL_* usage code
  const char *text;   // kMacroText: string to type
};

struct MacroPreset {
  const char *name;
  MacroSlot slots[CYPHER_KEYS_MACRO_SLOTS];
};

// Convenience initializers for config/Macros.h (keep the tables readable).
#define MACRO_EMPTY \
  { "", kMacroNone, kModNone, 0, 0, nullptr }
#define MACRO_COMBO(label, mods, key) \
  { label, kMacroCombo, (mods), (uint8_t)(key), 0, nullptr }
#define MACRO_MEDIA(label, usage) \
  { label, kMacroConsumer, kModNone, 0, (usage), nullptr }
#define MACRO_TEXT(label, str) \
  { label, kMacroText, kModNone, 0, 0, (str) }

#endif
