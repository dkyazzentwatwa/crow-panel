#ifndef CYPHER_KEYS_TRACKPAD_H
#define CYPHER_KEYS_TRACKPAD_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include <Arduino.h>

class KeysTouch;
class HidBackend;

// Full-screen relative trackpad: a large move surface, a right-edge scroll
// strip, and left/right click buttons that also support press-and-drag. All
// motion is fed to HidBackend as relative mouse reports.
class Trackpad {
 public:
  void reset();
  // Consume the current touch state and emit mouse reports. Call once per loop
  // while the deck is in trackpad mode.
  void update(KeysTouch &touch, HidBackend &hid);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g, const DeckTheme &theme) const;
#endif

 private:
  enum Zone : uint8_t { kZoneNone, kZonePad, kZoneScroll, kZoneLeft, kZoneRight };
  static Zone zoneAt(int16_t x, int16_t y);

  Zone active_ = kZoneNone;
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  int16_t startX_ = 0;
  int16_t startY_ = 0;
  uint32_t startMs_ = 0;
  int16_t scrollAccum_ = 0;
  bool moved_ = false;
};

#endif
