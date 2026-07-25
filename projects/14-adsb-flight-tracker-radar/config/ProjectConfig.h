#ifndef ADSB_RADAR_PROJECT_CONFIG_H
#define ADSB_RADAR_PROJECT_CONFIG_H

// Per-project overrides go here, BEFORE the AppConfig include: AppConfig.h only
// fills in defaults for flags left undefined.
//
// Note: overrides here only reach files that include this header (the .ino and
// this project's src/). The shared library's .cpp files compile with AppConfig
// defaults; flags that gate shared code (USE_WIFI in CrowNetworkClient.cpp,
// USE_DISPLAY in DisplayBringup.cpp) must be set as compiler -D flags instead:
//   EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" ./scripts/compile-all.sh

// --- ADS-B data feed ---------------------------------------------------------
// Free and keyless. airplanes.live is primary (proven on ESP32 in the mircemk
// reference); adsb.fi is the drop-in, same-schema fallback. The point query is
// appended at fetch time as  <base>/point/<lat>/<lon>/<radiusNM>.
#define ADSB_API_BASE          "https://api.airplanes.live/v2"
#define ADSB_API_FALLBACK_BASE "https://opendata.adsb.fi/api/v3"
#define ADSB_USER_AGENT        "CrowPanel-AirRadar/1.0"

// --- Home position, ranges, cadence -----------------------------------------
// Committed placeholder (JFK). Put your REAL coordinates in config/Location.h
// (gitignored copy of Location.example.h) so a home location is never
// committed. Location.h is included first so it wins over these defaults.
#if __has_include("Location.h")
#include "Location.h"
#endif
#ifndef ADSB_HOME_LAT
#define ADSB_HOME_LAT 40.6413   // decimal degrees (+N)
#endif
#ifndef ADSB_HOME_LON
#define ADSB_HOME_LON -73.7781  // decimal degrees (+E)
#endif
#ifndef ADSB_RANGE_NM
#define ADSB_RANGE_NM 54        // API query radius in NM (<=250); ~100 km
#endif
#ifndef ADSB_RANGE_KM
#define ADSB_RANGE_KM 100       // initial display ring; cycles 20/40/60/80/100
#endif
#ifndef ADSB_POLL_INTERVAL_MS
#define ADSB_POLL_INTERVAL_MS 5000  // clamped to >=1000 (adsb.fi/live = 1 req/s)
#endif
// POSIX TZ string for the on-screen NTP clock (default US Eastern to match JFK).
#ifndef ADSB_TZ
#define ADSB_TZ "EST5EDT,M3.2.0,M11.1.0"
#endif

// Short label for the on-screen LOCATION panel (your site/city name).
#ifndef ADSB_SITE_NAME
#define ADSB_SITE_NAME "HOME"
#endif

// Geomagnetic Kp threshold used by the Aurora screen. Lower this if you live
// farther north and want "likely" to trigger sooner.
#ifndef ADSB_WORLD_KP_THRESHOLD
#define ADSB_WORLD_KP_THRESHOLD 6
#endif

// Poll the live feed from a background FreeRTOS task on core 0 (keeps the sweep
// smooth) vs. a simple in-loop fetch. Set 0 for the in-loop fallback.
#ifndef ADSB_POLL_TASK
#define ADSB_POLL_TASK 1
#endif

// --- Touch orientation -------------------------------------------------------
// The GT911 reports panel-native coordinates. These map them to screen space:
// swap first, then invert. The defaults are the identity mapping, which is what
// the V1.2 CrowPanel Advanced 7" reports.
//
// This used to be handled by trying eight candidate transforms per tap and
// taking the first that landed on any control - which is a lottery, not a
// calibration: a single reading could select an aircraft via its swapped twin.
// If a fresh board disagrees, build once with ADSB_TOUCH_AUTOPROBE=1, tap each
// corner, read the `touch` log lines to see which mapping is consistent, then
// pin it here and turn the probe back off.
#ifndef ADSB_TOUCH_SWAP_XY
#define ADSB_TOUCH_SWAP_XY 0
#endif
#ifndef ADSB_TOUCH_INVERT_X
#define ADSB_TOUCH_INVERT_X 0
#endif
#ifndef ADSB_TOUCH_INVERT_Y
#define ADSB_TOUCH_INVERT_Y 0
#endif
#ifndef ADSB_TOUCH_AUTOPROBE
#define ADSB_TOUCH_AUTOPROBE 0
#endif

// --- Wi-Fi credentials (gitignored copy of WiFiSecrets.example.h) ------------
// Only meaningful when built with -DUSE_WIFI=1. Empty defaults keep every build
// compiling; the network client warns and runs log-only.
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
