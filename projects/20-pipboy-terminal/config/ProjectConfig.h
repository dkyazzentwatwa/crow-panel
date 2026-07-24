#ifndef PIPBOY_TERMINAL_PROJECT_CONFIG_H
#define PIPBOY_TERMINAL_PROJECT_CONFIG_H

#include <AppConfig.h>

#ifndef USE_PIPBOY_SD
#define USE_PIPBOY_SD 0
#endif

#ifndef USE_PIPBOY_AUDIO
#define USE_PIPBOY_AUDIO 0
#endif

#ifndef PIPBOY_SDMMC_1BIT
#define PIPBOY_SDMMC_1BIT 1
#endif

#ifndef PIPBOY_ROOT_DIR
#define PIPBOY_ROOT_DIR "/pipboy"
#endif

#ifndef PIPBOY_AUDIO_DIR
#define PIPBOY_AUDIO_DIR PIPBOY_ROOT_DIR "/audio"
#endif

#ifndef PIPBOY_IMAGE_DIR
#define PIPBOY_IMAGE_DIR PIPBOY_ROOT_DIR "/images"
#endif

#ifndef PIPBOY_LOG_DIR
#define PIPBOY_LOG_DIR PIPBOY_ROOT_DIR "/logs"
#endif

#ifndef PIPBOY_MAP_PATH
#define PIPBOY_MAP_PATH PIPBOY_IMAGE_DIR "/wasteland-map.bmp"
#endif

#ifndef PIPBOY_AUDIO_SAMPLE_RATE
#define PIPBOY_AUDIO_SAMPLE_RATE 16000
#endif

// A local default makes the showpiece work before a location is configured.
#ifndef PIPBOY_WEATHER_LAT
#define PIPBOY_WEATHER_LAT 36.1699f
#endif
#ifndef PIPBOY_WEATHER_LON
#define PIPBOY_WEATHER_LON -115.1398f
#endif
#ifndef PIPBOY_WEATHER_LABEL
#define PIPBOY_WEATHER_LABEL "MOJAVE OUTPOST"
#endif

// POSIX timezone applied after the hosted C6 has obtained NTP. Change this
// locally if the prop will be used elsewhere; it is intentionally independent
// from Wi-Fi credentials.
#ifndef PIPBOY_TZ
#define PIPBOY_TZ "PST8PDT,M3.2.0,M11.1.0"
#endif

// Wi-Fi is opt-in and credentials remain in a gitignored local header.
#if USE_WIFI && __has_include("WiFiSecrets.h")
#include "WiFiSecrets.h"
#endif
#ifndef PIPBOY_WIFI_SSID
#define PIPBOY_WIFI_SSID ""
#endif
#ifndef PIPBOY_WIFI_PASSWORD
#define PIPBOY_WIFI_PASSWORD ""
#endif

#endif
