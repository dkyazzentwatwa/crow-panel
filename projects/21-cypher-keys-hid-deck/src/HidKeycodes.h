#ifndef CYPHER_KEYS_HID_KEYCODES_H
#define CYPHER_KEYS_HID_KEYCODES_H

#include "HidTypes.h"
#include <Arduino.h>

// Translate one app key (ASCII or kKey*/KEY_* constant) to a USB HID usage and
// whether it needs Shift. Returns false if the key has no mapping.
bool hidUsageForKey(uint8_t key, uint8_t &usage, bool &needsShift);

// Convert the app's HidMod bitmask (kModCmd/Shift/Opt/Ctrl) to a HID modifier
// byte (left GUI/Shift/Alt/Ctrl bits).
uint8_t hidModifierByte(uint8_t mods);

#endif
