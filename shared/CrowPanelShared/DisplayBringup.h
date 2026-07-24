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

namespace CrowDisplay {
// One touch contact from the GT911 (which tracks up to 5). `id` is the
// controller's track id - stable while a finger stays down - so callers can
// follow individual contacts across samples for multi-touch gestures.
struct TouchPointData {
  int16_t x;
  int16_t y;
  uint8_t id;
};
}  // namespace CrowDisplay

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

// Underlying Arduino_GFX draw device (Arduino_DSI_Display derives from it).
// Forward-declared so callers that only need the simple status API don't
// have to pull in the whole Arduino_GFX header.
class Arduino_GFX;

namespace CrowDisplay {
// Initializes panel, backlight, GT911 touch, and the status screen.
// Returns false if the panel setup failed (Serial keeps working).
//
// manualFlush: default false keeps Arduino_GFX's auto_flush=true, so every draw
// call cache-syncs immediately (the behavior every existing project relies on).
// Pass true to build the panel with auto_flush=false: draws only touch the
// cached PSRAM framebuffer and nothing appears until the caller invokes flush().
// This turns Arduino_GFX's per-pixel esp_cache_msync into one sync per frame -
// an order-of-magnitude win for text-heavy UIs that redraw often.
bool begin(const HardwareProfile &profile, const char *title,
           bool manualFlush = false);

// Push the cached framebuffer to the panel. No-op unless begin() was called
// with manualFlush=true. The region overload syncs only the given rectangle
// (clamped to the panel) for hot paths like single-key feedback.
void flush();
void flush(int16_t x, int16_t y, int16_t w, int16_t h);

// Panel backlight, 0-255, over the LEDC PWM channel begin() attaches. 0 is
// fully dark - the panel is still rendering, you just cannot see it, so callers
// that expose this to a user should keep a usable floor. No-op before begin().
void setBacklight(uint8_t level);
uint8_t backlight();

// Updates one of the 6 status rows (index 0-5).
void setLine(uint8_t index, const String &text);

// Call once per loop(); polls touch and logs/marks tap coordinates.
void tick();

// Rich-rendering hooks for project dashboards (FieldOpsDashboard etc.):
// the raw draw device and the current touch point.
Arduino_GFX *canvas();                       // null until begin() succeeds
bool touchPoint(int16_t &x, int16_t &y);     // true while a finger is down

// Multi-touch variant: fills up to maxPoints contacts from the same
// 8 ms-throttled GT911 sample that feeds touchPoint() (point 0 is the same
// contact touchPoint() reports). Returns the contact count, 0-5.
uint8_t touchPoints(TouchPointData *out, uint8_t maxPoints);
}  // namespace CrowDisplay

#elif USE_DISPLAY

// USE_DISPLAY=1 on a non-P4 target (e.g. a generic fallback FQBN): keep
// the build green with no-op stubs so the flag matrix stays meaningful.
class Arduino_GFX;

namespace CrowDisplay {
inline bool begin(const HardwareProfile &, const char *, bool = false) { return false; }
inline void flush() {}
inline void flush(int16_t, int16_t, int16_t, int16_t) {}
inline void setBacklight(uint8_t) {}
inline uint8_t backlight() { return 0; }
inline void setLine(uint8_t, const String &) {}
inline void tick() {}
inline Arduino_GFX *canvas() { return nullptr; }
inline bool touchPoint(int16_t &, int16_t &) { return false; }
inline uint8_t touchPoints(TouchPointData *, uint8_t) { return 0; }
}  // namespace CrowDisplay

#endif

#endif
