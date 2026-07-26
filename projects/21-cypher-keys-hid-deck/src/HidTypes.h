#ifndef CYPHER_KEYS_HID_TYPES_H
#define CYPHER_KEYS_HID_TYPES_H

#include "../config/ProjectConfig.h"
#include <CrowHidTypes.h>  // shared: HidMod, kKey*/kCc*, MacroKind, MacroSlot, MACRO_*

// The HID vocabulary (HidMod, key/consumer constants, MacroKind, MacroSlot, the
// MACRO_* initializers) now lives in the shared CrowHid stack. MacroPreset stays
// project-local because it is sized by CYPHER_KEYS_MACRO_SLOTS, a per-project
// tuning value the shared library never sees (CLAUDE.md, three-layer flag rule).
struct MacroPreset {
  const char *name;
  MacroSlot slots[CYPHER_KEYS_MACRO_SLOTS];
};

#endif
