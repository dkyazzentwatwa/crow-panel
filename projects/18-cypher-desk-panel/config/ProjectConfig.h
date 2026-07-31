#ifndef CYPHER_DESK_PANEL_PROJECT_CONFIG_H
#define CYPHER_DESK_PANEL_PROJECT_CONFIG_H

#include <AppConfig.h>

// The original Cardputer app stores notes under this SD path. Keeping it here
// makes a card portable between the source app and this CrowPanel port.
#ifndef CYPHER_DESK_NOTES_DIR
#define CYPHER_DESK_NOTES_DIR "/cypher-puter/desk/notes"
#endif

#ifndef CYPHER_DESK_ROOT_DIR
#define CYPHER_DESK_ROOT_DIR "/cypher-puter/desk"
#endif
#ifndef CYPHER_DESK_AUDIO_DIR
#define CYPHER_DESK_AUDIO_DIR CYPHER_DESK_ROOT_DIR "/audio"
#endif
#ifndef CYPHER_DESK_EXPORTS_DIR
#define CYPHER_DESK_EXPORTS_DIR CYPHER_DESK_ROOT_DIR "/exports"
#endif
#ifndef CYPHER_DESK_DATA_DIR
#define CYPHER_DESK_DATA_DIR CYPHER_DESK_ROOT_DIR "/.desk"
#endif
#ifndef CYPHER_DESK_RECORDINGS_DIR
#define CYPHER_DESK_RECORDINGS_DIR CYPHER_DESK_ROOT_DIR "/recordings"
#endif
#ifndef CYPHER_DESK_MUSIC_DIR
#define CYPHER_DESK_MUSIC_DIR CYPHER_DESK_ROOT_DIR "/music"
#endif
#ifndef CYPHER_DESK_PODCASTS_DIR
#define CYPHER_DESK_PODCASTS_DIR CYPHER_DESK_ROOT_DIR "/podcasts"
#endif
#ifndef CYPHER_DESK_VIDEO_DIR
#define CYPHER_DESK_VIDEO_DIR CYPHER_DESK_ROOT_DIR "/video"
#endif
#ifndef CYPHER_DESK_DOCUMENTS_DIR
#define CYPHER_DESK_DOCUMENTS_DIR CYPHER_DESK_ROOT_DIR "/documents"
#endif
#ifndef CYPHER_DESK_CALENDAR_DIR
#define CYPHER_DESK_CALENDAR_DIR CYPHER_DESK_ROOT_DIR "/calendar"
#endif
#ifndef CYPHER_DESK_CONTACTS_DIR
#define CYPHER_DESK_CONTACTS_DIR CYPHER_DESK_ROOT_DIR "/contacts"
#endif
#ifndef CYPHER_DESK_BACKUPS_DIR
#define CYPHER_DESK_BACKUPS_DIR CYPHER_DESK_ROOT_DIR "/backups"
#endif
#ifndef CYPHER_DESK_CACHE_DIR
#define CYPHER_DESK_CACHE_DIR CYPHER_DESK_ROOT_DIR "/cache"
#endif

// Default remains RAM-backed so baseline/display builds never require an SD
// card. Enable this flag to mount the CrowPanel's SD_MMC interface.
#ifndef USE_CYPHER_DESK_SD
#define USE_CYPHER_DESK_SD 0
#endif

#ifndef CYPHER_DESK_SDMMC_1BIT
#define CYPHER_DESK_SDMMC_1BIT 1
#endif

#ifndef USE_CYPHER_DESK_AUDIO
#define USE_CYPHER_DESK_AUDIO 0
#endif

// Microphone pins and the RX data format have not yet passed a CrowPanel bench
// test. This flag compiles the guarded recorder surface but does not claim that
// physical recording is proven.
#ifndef USE_CYPHER_DESK_RECORDER
#define USE_CYPHER_DESK_RECORDER 0
#endif

#ifndef USE_CYPHER_DESK_MEDIA
#define USE_CYPHER_DESK_MEDIA USE_CYPHER_DESK_AUDIO
#endif

// Weather is opt-in at the feature level and still requires a user-configured
// location plus WifiService's verified-internet state at runtime.
#ifndef USE_CYPHER_DESK_WEATHER
#define USE_CYPHER_DESK_WEATHER USE_WIFI
#endif

#ifndef CYPHER_DESK_WEATHER_REFRESH_MS
#define CYPHER_DESK_WEATHER_REFRESH_MS (15UL * 60UL * 1000UL)
#endif

#ifndef CYPHER_DESK_CONNECTIVITY_HOST
#define CYPHER_DESK_CONNECTIVITY_HOST "connectivitycheck.gstatic.com"
#endif
#ifndef CYPHER_DESK_CONNECTIVITY_PATH
#define CYPHER_DESK_CONNECTIVITY_PATH "/generate_204"
#endif
#ifndef CYPHER_DESK_CONNECTIVITY_PORT
#define CYPHER_DESK_CONNECTIVITY_PORT 80
#endif

#ifndef CYPHER_DESK_LOW_SPACE_BYTES
#define CYPHER_DESK_LOW_SPACE_BYTES (32ULL * 1024ULL * 1024ULL)
#endif

#ifndef CYPHER_DESK_TIMEZONE
#define CYPHER_DESK_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"
#endif

#ifndef CYPHER_DESK_AUTOSAVE_MS
#define CYPHER_DESK_AUTOSAVE_MS 1800
#endif

#ifndef CYPHER_DESK_AUDIO_SAMPLE_RATE
#define CYPHER_DESK_AUDIO_SAMPLE_RATE 16000
#endif

// Polling the GT911 faster than the panel refreshes just returns empty frames,
// so 16 ms is the floor worth paying for. Matches project 21.
#ifndef CYPHER_DESK_TOUCH_POLL_MS
#define CYPHER_DESK_TOUCH_POLL_MS 16
#endif

#ifndef CYPHER_DESK_TOUCH_MIN_X
#define CYPHER_DESK_TOUCH_MIN_X 0
#endif
#ifndef CYPHER_DESK_TOUCH_MAX_X
#define CYPHER_DESK_TOUCH_MAX_X 1023
#endif
#ifndef CYPHER_DESK_TOUCH_MIN_Y
#define CYPHER_DESK_TOUCH_MIN_Y 0
#endif
#ifndef CYPHER_DESK_TOUCH_MAX_Y
#define CYPHER_DESK_TOUCH_MAX_Y 599
#endif
#ifndef CYPHER_DESK_TOUCH_SWAP_XY
#define CYPHER_DESK_TOUCH_SWAP_XY 0
#endif
#ifndef CYPHER_DESK_TOUCH_INVERT_X
#define CYPHER_DESK_TOUCH_INVERT_X 0
#endif
#ifndef CYPHER_DESK_TOUCH_INVERT_Y
#define CYPHER_DESK_TOUCH_INVERT_Y 0
#endif

#endif
