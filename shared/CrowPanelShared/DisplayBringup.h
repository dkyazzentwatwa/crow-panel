#ifndef CROW_PANEL_DISPLAY_BRINGUP_H
#define CROW_PANEL_DISPLAY_BRINGUP_H

#include <Arduino.h>
#include "AppConfig.h"
#include "HardwareProfile.h"

// Minimal, honest display bring-up for the CrowPanel Advanced 7-inch
// ESP32-P4: MIPI-DSI panel + GT911 touch, driving a single status screen
// that mirrors what each project prints to Serial.
//
// Rendering uses the Adafruit-GFX-style API (setCursor/print/fillRect)
// through Arduino_GFX's Arduino_DSI_Display - the Adafruit-GFX-compatible
// driver for this panel (Adafruit's own library has no MIPI-DSI backend).
// No LVGL. Deliberately not a full multi-screen UI - that comes after the
// panel is hardware-verified. See docs/hardware-bringup-checklist.md
// Stages 3-4.

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

// Underlying Arduino_GFX draw device (Arduino_DSI_Display derives from it).
// Forward-declared so callers that only need the simple status API don't
// have to pull in the whole Arduino_GFX header.
class Arduino_GFX;

namespace CrowDisplay {
// Initializes panel, backlight, GT911 touch, and the status screen.
// Returns false if the panel setup failed (Serial keeps working).
bool begin(const HardwareProfile &profile, const char *title);

// Updates one of the 6 status rows (index 0-5).
void setLine(uint8_t index, const String &text);

// Call once per loop(); polls touch and logs/marks tap coordinates.
void tick();

// Rich-rendering hooks for project dashboards (FieldOpsDashboard etc.):
// the raw draw device and the current touch point.
Arduino_GFX *canvas();                       // null until begin() succeeds
bool touchPoint(int16_t &x, int16_t &y);     // true while a finger is down
}  // namespace CrowDisplay

#elif USE_DISPLAY

// USE_DISPLAY=1 on a non-P4 target (e.g. a generic fallback FQBN): keep
// the build green with no-op stubs so the flag matrix stays meaningful.
class Arduino_GFX;

namespace CrowDisplay {
inline bool begin(const HardwareProfile &, const char *) { return false; }
inline void setLine(uint8_t, const String &) {}
inline void tick() {}
inline Arduino_GFX *canvas() { return nullptr; }
inline bool touchPoint(int16_t &, int16_t &) { return false; }
}  // namespace CrowDisplay

#endif

#endif
