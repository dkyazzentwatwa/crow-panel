#ifndef CYPHER_KEYS_MACRO_PRESETS_H
#define CYPHER_KEYS_MACRO_PRESETS_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include "HidTypes.h"
#include <Arduino.h>

// Owns the compiled preset table (config/Macros.h), the active preset index,
// and NVS persistence of the last-used preset. Pure model + hit-testing; the
// controller draws the tabs and grid and fires slots through HidBackend.
class MacroPresets {
 public:
  void begin();  // loads presets and the remembered active index from NVS

  uint8_t presetCount() const;
  uint8_t activeIndex() const { return active_; }
  const MacroPreset &active() const;
  const MacroPreset &preset(uint8_t index) const;
  const char *activeName() const;

  void setActive(uint8_t index);  // clamps + persists
  void next();                    // cycle, persists
  bool selectByName(const String &name);  // case-insensitive prefix match

  uint8_t slotCount() const { return CYPHER_KEYS_MACRO_SLOTS; }
  const MacroSlot &slot(uint8_t index) const;  // in the active preset

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g, const DeckTheme &theme) const;
#endif

  // Touch hit-testing against the tab bar / macro grid. Compile-safe in every
  // build (pure integer geometry); returns -1 for a miss.
  int8_t hitTab(int16_t x, int16_t y) const;
  int8_t hitSlot(int16_t x, int16_t y) const;

 private:
  void persist() const;
  uint8_t active_ = 0;
  uint8_t count_ = 0;
};

#endif
