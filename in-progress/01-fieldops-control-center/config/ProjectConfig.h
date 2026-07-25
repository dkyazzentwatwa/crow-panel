#ifndef FIELDOPS_PROJECT_CONFIG_H
#define FIELDOPS_PROJECT_CONFIG_H

// Per-project overrides go here, BEFORE the AppConfig include: AppConfig.h
// only fills in defaults for flags left undefined. The CROWPANEL_P4_7IN_*
// names may be used here even though AppConfig.h defines them later,
// because macros expand where they are USED, not where they are defined.
//
// Examples:
// #define USE_LORA_DRIVER 1
// #define CROWPANEL_HARDWARE_PROFILE CROWPANEL_P4_7IN_V1_1
//
// Note: overrides here only reach files that include this header (the .ino
// and this project's src/). The shared library's .cpp files compile with
// AppConfig defaults; flags that gate shared code (e.g. USE_WIFI in
// CrowNetworkClient.cpp) must be set as compiler -D flags instead:
//   EXTRA_FLAGS="-DUSE_WIFI=1" ./scripts/compile-all.sh

#define FIELDOPS_API_ENDPOINT "http://localhost:8787"

// Local hardware worksheet (gitignored copy of Pins.example.h): LoRa
// radio parameter overrides.
#if __has_include("Pins.h")
#include "Pins.h"
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

#include <AppConfig.h>

#endif
