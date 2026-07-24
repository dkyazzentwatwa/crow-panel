#ifndef VISION_CAM_PROJECT_CONFIG_H
#define VISION_CAM_PROJECT_CONFIG_H

// Per-project overrides go here, BEFORE the AppConfig include: AppConfig.h
// only fills in defaults for flags left undefined. The CROWPANEL_P4_7IN_*
// names may be used here even though AppConfig.h defines them later,
// because macros expand where they are USED, not where they are defined.
//
// Note: overrides here only reach files that include this header (the .ino
// and this project's src/). The shared library's .cpp files compile with
// AppConfig defaults; flags that gate shared code must be set as compiler
// -D flags instead. That applies to BOTH shared files this project leans on:
//   EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CAMERA_DRIVER=1" ./scripts/compile-all.sh
// Defining USE_CAMERA_DRIVER here alone would leave CameraBringup.cpp compiled
// as no-op stubs and the camera silently dead.

// --- Capture / storage -----------------------------------------------------

// SD stills and video clips. Off by default so the baseline build needs no
// card. Gates project-local code only, so a define here is sufficient.
#ifndef USE_CAM_SD
#define USE_CAM_SD 0
#endif

// SD_MMC bus width. 1-bit is the conservative default Project 20 proved on this
// panel; it sustains roughly 700 KB/s, which is what the record defaults below
// are sized against.
#ifndef VISIONCAM_SDMMC_1BIT
#define VISIONCAM_SDMMC_1BIT 1
#endif

// Recording defaults. 640x480 at q75 lands near 40 KB/frame, so 10 fps is about
// 400 KB/s - inside the 1-bit SD_MMC budget with headroom for filesystem
// overhead. The UI exposes all three and reports the fps actually achieved.
#ifndef VISIONCAM_REC_WIDTH
#define VISIONCAM_REC_WIDTH 640
#endif
#ifndef VISIONCAM_REC_HEIGHT
#define VISIONCAM_REC_HEIGHT 480
#endif
#ifndef VISIONCAM_REC_QUALITY
#define VISIONCAM_REC_QUALITY 75
#endif
#ifndef VISIONCAM_REC_FPS
#define VISIONCAM_REC_FPS 10
#endif

// --- Physical shutter ------------------------------------------------------

// The BOOT button doubles as a shutter release. Safe to reuse: BOOT is a
// strapping pin sampled by the ROM bootloader at reset, long before this sketch
// runs, so reading it as an ordinary input afterwards does not interfere with
// entering download mode (hold BOOT, tap RESET - still works).
//
// GPIO36 per the V1.2 schematic (net DOWNLOAD_BOOT, driven by a KEY-4.5X4.5
// tactile switch marked "LOW: Download Boot") and the core's own esp32p4
// variant header ("BOOT_MODE2 36 pullup"). Active LOW.
#ifndef VISIONCAM_SHUTTER_PIN
#define VISIONCAM_SHUTTER_PIN 36
#endif

// The other boot strap (GPIO35, net SPI_BOOT). Not used as an input; its level
// is displayed alongside the shutter pin purely so the correct button pin can
// be confirmed by observation rather than inferred from a schematic PDF.
#ifndef VISIONCAM_SHUTTER_ALT_PIN
#define VISIONCAM_SHUTTER_ALT_PIN 35
#endif

// Milliseconds a press must be stable before it counts. Tactile switches bounce
// for a few ms; 40 is comfortably past that without feeling laggy.
#ifndef VISIONCAM_SHUTTER_DEBOUNCE_MS
#define VISIONCAM_SHUTTER_DEBOUNCE_MS 40
#endif

// --- Streaming -------------------------------------------------------------

// HTTP ports. The MJPEG stream gets its own socket on purpose: WebServer's
// handleClient() cannot hold a multipart/x-mixed-replace response open without
// stalling the render loop, so :80 serves pages and :81 pushes frames.
#ifndef VISIONCAM_HTTP_PORT
#define VISIONCAM_HTTP_PORT 80
#endif
#ifndef VISIONCAM_STREAM_PORT
#define VISIONCAM_STREAM_PORT 81
#endif

// Soft-AP credentials (gitignored copy of CamSecrets.example.h). Without it the
// AP falls back to a MAC-derived SSID and the placeholder password below, which
// the UI flags as insecure. An open AP is never offered: this device streams a
// live camera feed, so the radio only ever comes up password-protected.
#if __has_include("CamSecrets.h")
#include "CamSecrets.h"
#endif
#ifndef VISIONCAM_AP_SSID
#define VISIONCAM_AP_SSID ""  // empty -> derive "CypherCam-XXXX" from the MAC
#endif
#ifndef VISIONCAM_AP_PASS
#define VISIONCAM_AP_PASS "changeme-cypher"
#endif

// Wi-Fi credentials for joining an existing network (gitignored copy of
// WiFiSecrets.example.h); only meaningful with -DUSE_WIFI=1. Empty defaults
// keep every build compiling and leave the device in AP-only mode.
#if __has_include("WiFiSecrets.h")
#include "WiFiSecrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

#include <AppConfig.h>

#endif
