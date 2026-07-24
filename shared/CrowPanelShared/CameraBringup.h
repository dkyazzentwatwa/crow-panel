#ifndef CROW_PANEL_CAMERA_BRINGUP_H
#define CROW_PANEL_CAMERA_BRINGUP_H

#include <Arduino.h>
#include "AppConfig.h"
#include "HardwareProfile.h"
#include "Sc2336Sensor.h"

// MIPI-CSI camera bring-up for the CrowPanel Advanced 7-inch ESP32-P4:
// SC2336 sensor -> CSI receiver -> ISP -> RGB565 frames in PSRAM.
//
// Shaped deliberately like DisplayBringup: a small namespace with a real
// implementation under the feature flag and inline no-ops otherwise, so the
// flag matrix stays meaningful and callers never need their own #ifdefs.
//
// WHY THIS IS POSSIBLE AT ALL, since the repo previously documented the
// opposite: `esp32-camera` has no ESP32-P4 port, but the P4 does not use it.
// Arduino core 3.3.8 already ships and links the ESP-IDF camera stack for this
// target - libesp_driver_cam.a (MIPI-CSI), libesp_driver_isp.a,
// libesp_driver_jpeg.a and libesp_driver_ppa.a all ship with their headers in
// the core's P4 library trees. The only piece missing from the core was the
// sensor register table, which is what Sc2336Sensor supplies.
//
// BRING-UP ORDER MATTERS. Call begin() AFTER CrowDisplay::begin():
//   - the MIPI D-PHY rail (LDO channel 3 @ 2500 mV) is shared between the DSI
//     panel and the CSI receiver, and Arduino_GFX acquires it during display
//     bring-up. Acquiring it here too is correct and refcounted, but doing it
//     in this order means a display-less build still powers the rail properly.
//   - the renderer blits into the DSI framebuffer, which must already exist.
// And call BOTH after SD_MMC, which must be mounted before the DSI framebuffer
// starts on this panel (device-proven; see Project 20).

namespace CrowCamera {

// One captured frame. `data` points into a driver-owned PSRAM buffer that stays
// valid until the next release() - do not hold it across frames, and do not
// free it.
struct Frame {
  uint16_t *data;      // RGB565, width * height pixels
  size_t bytes;
  uint16_t width;
  uint16_t height;
  uint32_t sequence;   // monotonic frame counter since begin()
  uint32_t captureMs;  // millis() when the frame completed
};

}  // namespace CrowCamera

#if USE_CAMERA_DRIVER && defined(CONFIG_IDF_TARGET_ESP32P4)

namespace CrowCamera {

// Powers the D-PHY rail, configures the sensor, creates the ISP processor and
// the CSI receiver, and allocates the PSRAM RGB565 frame buffers to rotate
// between. Returns false on any failure; lastError() explains which.
//
// Does not start the sensor streaming - call start() once the app is ready to
// consume frames, so the first frames are not thrown away.
bool begin(const HardwareProfile &profile);

bool start();  // sensor streaming on, receiver armed
bool stop();   // sensor to standby, receiver stopped
void end();    // full teardown; safe to call without a successful begin()

// Fetches the next completed frame. Returns false if none arrived within
// timeoutMs. On success the caller owns the frame until release().
//
// CONTRACT: release() every frame you acquire, and hold at most one at a time.
// Frames are not copied - `data` points at a live capture buffer. The driver
// keeps running if you are slow (it recycles the stalest queued frame and bumps
// dropCount), but if the app holds every buffer at once there is nothing left
// to capture into and the stream stalls until you release one.
bool acquire(Frame &out, uint32_t timeoutMs = 100);
void release(const Frame &frame);

bool ready();
bool streaming();
const char *lastError();

// Frames the driver produced vs. frames it had to drop because no buffer was
// free. A steadily climbing drop count means the app is not consuming fast
// enough - surface it rather than hiding it.
uint32_t frameCount();
uint32_t dropCount();

// The sensor, for exposure/gain/flip control and diagnostics. Null until a
// successful begin().
Sc2336Sensor *sensor();

// --- Image statistics and colour -------------------------------------------
//
// The Arduino core ships the ISP's statistics ENGINES but not esp_video's
// software pipeline controller that normally drives them. So the hardware can
// measure the image, but nothing in the core decides what to do about it -
// that policy has to live in the application. These calls expose the
// measurements and the one knob that acts on them; the control loops that use
// them live in the project (see CamPipeline).

// Mean luminance across the AE engine's 5x5 block grid.
//
// The units are not documented in the public headers - treat the value as a
// relative signal, which is all a feedback loop needs. `blocks` optionally
// receives the raw 25-block grid for diagnostics.
bool meanLuminance(uint32_t &mean, int *blocks = nullptr, size_t blockCount = 0);

// Gray-world white-balance estimate from the AWB engine's white-patch sums.
// Returns false when too few white patches were found to be meaningful, which
// is the honest answer for a scene with no neutral reference in it.
bool whiteBalanceEstimate(float &redGain, float &blueGain, uint32_t &patchCount);

// Applies per-channel gain through the Color Correction Matrix diagonal.
//
// Deliberately NOT the WBG block: its gain registers are fixed-point and the
// format is not documented in any public header, so writing them would be
// guesswork. The CCM takes a plain float matrix, and the AWB header itself
// recommends this route for sensors (like the SC2336) that expose no
// per-channel RGB gain of their own.
bool setColorGains(float red, float green, float blue);
void colorGains(float &red, float &green, float &blue);

void printStatus(Print &out);

}  // namespace CrowCamera

#else

// USE_CAMERA_DRIVER=0, or a non-P4 target: no-op stubs so every build stays
// green and callers need no #ifdefs. acquire() always fails, which is the
// honest answer - there is no camera here.
namespace CrowCamera {
inline bool begin(const HardwareProfile &) { return false; }
inline bool start() { return false; }
inline bool stop() { return false; }
inline void end() {}
inline bool acquire(Frame &, uint32_t = 100) { return false; }
inline void release(const Frame &) {}
inline bool ready() { return false; }
inline bool streaming() { return false; }
inline const char *lastError() { return "built without USE_CAMERA_DRIVER"; }
inline uint32_t frameCount() { return 0; }
inline uint32_t dropCount() { return 0; }
inline Sc2336Sensor *sensor() { return nullptr; }
inline bool meanLuminance(uint32_t &, int * = nullptr, size_t = 0) { return false; }
inline bool whiteBalanceEstimate(float &, float &, uint32_t &) { return false; }
inline bool setColorGains(float, float, float) { return false; }
inline void colorGains(float &r, float &g, float &b) { r = g = b = 1.0f; }
inline void printStatus(Print &out) {
  out.println(F("[camera] disabled (build with -DUSE_CAMERA_DRIVER=1)"));
}
}  // namespace CrowCamera

#endif

#endif
