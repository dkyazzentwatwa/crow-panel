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

// Gates the Music and Podcast apps. This used to be an alias for
// USE_CYPHER_DESK_AUDIO; it is its own switch now so a silent build can still
// carry the library UI.
#ifndef USE_CYPHER_DESK_MEDIA
#define USE_CYPHER_DESK_MEDIA USE_CYPHER_DESK_AUDIO
#endif

// MJPEG-in-AVI playback through the P4's hardware JPEG decoder and PPA scaler.
// Needs display + SD; the audio track needs USE_CYPHER_DESK_AUDIO as well, and
// a clip without one simply plays silently.
#ifndef USE_CYPHER_DESK_VIDEO
#define USE_CYPHER_DESK_VIDEO 0
#endif

// Decoder buffers are sized from this once at boot. A clip larger than this is
// refused with its real dimensions in the message rather than half-decoded.
// 640x480 costs ~614 KB of PSRAM for the pixel buffer.
#ifndef CYPHER_DESK_VIDEO_MAX_W
#define CYPHER_DESK_VIDEO_MAX_W 640
#endif
#ifndef CYPHER_DESK_VIDEO_MAX_H
#define CYPHER_DESK_VIDEO_MAX_H 480
#endif

// Flash budget: everything now fits together, with room to spare.
//
//   display+sd+audio+media+video+wifi+recorder   1.86 MB   59% of the app slot
//
// It did not, briefly: adding video took the kitchen-sink build to 3.21 MB
// against a 3 MB partition. The cause turned out not to be video at all.
// U8g2 declares its fonts as file-scope consts in the header, which is
// internal linkage in C++, so every .cpp that named u8g2_font_cubic11_h_cjk
// got a private 337,650-byte copy the linker cannot merge. FIVE copies were
// reaching the image - 1.69 MB, over half the partition, for one font.
// Routing all compact text through DeskUi::smallText (defined in exactly one
// translation unit, DeskWidgets.cpp) recovered 1.35 MB.
//
// If a build ever jumps by ~330 KB for no obvious reason, check for a new
// #include <U8g2lib.h> before anything else.

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

// Legacy rate for the ambience loops and recorder WAVs this project shipped
// with. Content at this rate still plays - the mixer resamples it.
#ifndef CYPHER_DESK_AUDIO_SAMPLE_RATE
#define CYPHER_DESK_AUDIO_SAMPLE_RATE 16000
#endif

// The I2S channel runs at one fixed rate for the life of the sketch and every
// source is resampled up to it. Reconfiguring the clock per track would mean
// stopping the channel mid-playback, and it would make the played-frame counter
// useless as a video sync clock.
#ifndef CYPHER_DESK_AUDIO_OUT_RATE
#define CYPHER_DESK_AUDIO_OUT_RATE 44100
#endif

// Software ring between the SD reader (loop context) and the mixer task, in
// output frames. Must be a power of two. 65536 frames is ~1.5 s at 44.1 kHz -
// enough to ride out a full-screen redraw or a slow directory read.
#ifndef CYPHER_DESK_AUDIO_RING_FRAMES
#define CYPHER_DESK_AUDIO_RING_FRAMES 65536
#endif

#ifndef CYPHER_DESK_AUDIO_MIC_RATE
#define CYPHER_DESK_AUDIO_MIC_RATE 16000
#endif

// Backlight. The idle level is deliberately never 0: the panel keeps
// rendering at 0, you just cannot see it, which reads as a crash.
#ifndef CYPHER_DESK_BRIGHTNESS
#define CYPHER_DESK_BRIGHTNESS 255
#endif
#ifndef CYPHER_DESK_IDLE_DIM_MS
#define CYPHER_DESK_IDLE_DIM_MS 90000
#endif
#ifndef CYPHER_DESK_IDLE_DIM_LEVEL
#define CYPHER_DESK_IDLE_DIM_LEVEL 28
#endif

// Polling the GT911 faster than the panel refreshes just returns empty frames,
// so 16 ms is the floor worth paying for. Matches project 21.
#ifndef CYPHER_DESK_TOUCH_POLL_MS
#define CYPHER_DESK_TOUCH_POLL_MS 16
#endif

// A release is only committed after this long with no contact, so one dropped
// GT911 frame mid-press can never read as a lift.
#ifndef CYPHER_DESK_TOUCH_RELEASE_DEBOUNCE_MS
#define CYPHER_DESK_TOUCH_RELEASE_DEBOUNCE_MS 30
#endif

// Proximity fallback in pixels for panels whose GT911 track ids churn: an
// unmatched point this close to a live contact is the same finger, not a new
// press.
#ifndef CYPHER_DESK_TOUCH_MATCH_RADIUS
#define CYPHER_DESK_TOUCH_MATCH_RADIUS 48
#endif

// Mac-like hold-repeat, applied only to backspace and the arrow keys.
#ifndef CYPHER_DESK_KEY_REPEAT_DELAY_MS
#define CYPHER_DESK_KEY_REPEAT_DELAY_MS 400
#endif
#ifndef CYPHER_DESK_KEY_REPEAT_MS
#define CYPHER_DESK_KEY_REPEAT_MS 60
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
