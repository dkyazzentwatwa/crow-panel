#ifndef RELAYOPS_PROJECT_CONFIG_H
#define RELAYOPS_PROJECT_CONFIG_H

// Per-project overrides go here, BEFORE the AppConfig include: AppConfig.h
// only fills in defaults for flags left undefined.
//
// Note: overrides here only reach files that include this header (the .ino
// and this project's src/). The shared library's .cpp files compile with
// AppConfig defaults; flags that gate shared code (e.g. USE_WIFI in
// CrowNetworkClient.cpp) must be set as compiler -D flags instead:
//   EXTRA_FLAGS="-DUSE_WIFI=1" ./scripts/compile-all.sh

// Optional backend for an event trail (POST /events). The hub itself is a
// server for its nodes; this is only the outbound log sink to the mock API.
#define RELAYOPS_API_ENDPOINT "http://localhost:8787"

// TCP port the on-panel web server listens on for node POSTs (USE_WIFI=1).
#ifndef RELAYOPS_SERVER_PORT
#define RELAYOPS_SERVER_PORT 80
#endif

// Local hardware worksheet (gitignored copy of Pins.example.h): optional
// on-board GPIO overrides (e.g. a status LED).
#if __has_include("Pins.h")
#include "Pins.h"
#endif

// Static controllable-device table. Copy Devices.example.h to Devices.h
// (gitignored) and edit for your network; the defaults below keep every
// build compiling and give the offline `set` demo two devices to toggle.
// Each row: RELAYOPS_DEVICE(<id>, <host>, <path>, <pin>). The hub sends
//   GET http://<host><path>?pin=<pin>&state=<0|1>
// to toggle. Nodes may also self-register at runtime via POST /register.
#if __has_include("Devices.h")
#include "Devices.h"
#endif
#ifndef RELAYOPS_STATIC_DEVICES
#define RELAYOPS_STATIC_DEVICES                              \
  RELAYOPS_DEVICE("shop-light", "127.0.0.1", "/gpio", 2)     \
  RELAYOPS_DEVICE("porch-relay", "127.0.0.1", "/gpio", 4)
#endif

// Wi-Fi credentials (gitignored copy of WiFiSecrets.example.h); only
// meaningful when built with -DUSE_WIFI=1. Empty defaults keep every
// build compiling; the network client warns and runs log-only.
#if __has_include("WiFiSecrets.h")
#include "WiFiSecrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// Location for the weather + aurora feeds (gitignored copy of
// Location.example.h). Defaults keep every build compiling.
#if __has_include("Location.h")
#include "Location.h"
#endif
#ifndef RELAYOPS_LAT
#define RELAYOPS_LAT 44.05
#endif
#ifndef RELAYOPS_LON
#define RELAYOPS_LON -123.09
#endif
#ifndef RELAYOPS_PLACE
#define RELAYOPS_PLACE "Eugene, OR"
#endif
#ifndef RELAYOPS_KP_THRESHOLD
#define RELAYOPS_KP_THRESHOLD 6
#endif

#include <AppConfig.h>

#endif
