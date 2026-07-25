#ifndef CYPHER_TUNE_MPC_PROJECT_CONFIG_H
#define CYPHER_TUNE_MPC_PROJECT_CONFIG_H

#include <AppConfig.h>

// Engine sample rate. All synthesis/mixing happens at this rate; SD WAVs may
// be any rate 8-48 kHz (the voice resampler converts on the fly).
#ifndef CYPHER_TUNE_ENGINE_RATE
#define CYPHER_TUNE_ENGINE_RATE 22050
#endif
// Back-compat alias (older docs/scripts referenced the original name).
// Idle backlight dimming. Only applies while the transport is stopped, so a
// playing loop never dims mid-bar. Generous by default.
#ifndef CYPHER_TUNE_IDLE_DIM_MS
#define CYPHER_TUNE_IDLE_DIM_MS 120000
#endif

// NVS namespace for persisted UI state (theme, brightness, idle dim).
#ifndef CYPHER_TUNE_NVS_NAMESPACE
#define CYPHER_TUNE_NVS_NAMESPACE "cyphertune"
#endif

#ifndef CYPHER_TUNE_AUDIO_SAMPLE_RATE
#define CYPHER_TUNE_AUDIO_SAMPLE_RATE CYPHER_TUNE_ENGINE_RATE
#endif

// I2S DMA geometry: render block = one DMA descriptor. Latency ~= DESC *
// BLOCK frames of queued audio (4 * 128 @ 22050 Hz = 23 ms ring; pad-to-
// speaker worst case ~29 ms). Raise DESC if the underrun counter moves.
#ifndef CYPHER_TUNE_BLOCK_FRAMES
#define CYPHER_TUNE_BLOCK_FRAMES 128
#endif
#ifndef CYPHER_TUNE_DMA_DESC
#define CYPHER_TUNE_DMA_DESC 4
#endif

#ifndef CYPHER_TUNE_VOICES
#define CYPHER_TUNE_VOICES 8
#endif

// Per-pad sample length clamp (2 s at 48 kHz = 187.5 KB).
#ifndef CYPHER_TUNE_MAX_SAMPLE_FRAMES
#define CYPHER_TUNE_MAX_SAMPLE_FRAMES 96000
#endif

// Master output volume 0-255.
#ifndef CYPHER_TUNE_MASTER_VOLUME
#define CYPHER_TUNE_MASTER_VOLUME 96
#endif

// SD-card WAV kits (off by default; needs USE_AUDIO=1 to be audible).
#ifndef USE_MPC_SD
#define USE_MPC_SD 0
#endif
#ifndef CYPHER_TUNE_SDMMC_1BIT
#define CYPHER_TUNE_SDMMC_1BIT 1
#endif
#ifndef CYPHER_TUNE_KIT_DIR
#define CYPHER_TUNE_KIT_DIR "/mpc/kits"
#endif

// Backing loops (one resident in PSRAM at a time; see LoopLibrary).
#ifndef CYPHER_TUNE_LOOP_DIR
#define CYPHER_TUNE_LOOP_DIR "/mpc/loops"
#endif

// Audio render task placement: loop()/UI own core 1, audio owns core 0.
#ifndef CYPHER_TUNE_AUDIO_TASK_CORE
#define CYPHER_TUNE_AUDIO_TASK_CORE 0
#endif
#ifndef CYPHER_TUNE_AUDIO_TASK_PRIO
#define CYPHER_TUNE_AUDIO_TASK_PRIO 10
#endif

// GT911 touch mapping (identity by default; flip/swap per unit if needed;
// same scheme as project 21's trackpad calibration).
#ifndef CYPHER_TUNE_TOUCH_SWAP_XY
#define CYPHER_TUNE_TOUCH_SWAP_XY 0
#endif
#ifndef CYPHER_TUNE_TOUCH_INVERT_X
#define CYPHER_TUNE_TOUCH_INVERT_X 0
#endif
#ifndef CYPHER_TUNE_TOUCH_INVERT_Y
#define CYPHER_TUNE_TOUCH_INVERT_Y 0
#endif
#ifndef CYPHER_TUNE_TOUCH_MIN_X
#define CYPHER_TUNE_TOUCH_MIN_X 0
#endif
#ifndef CYPHER_TUNE_TOUCH_MAX_X
#define CYPHER_TUNE_TOUCH_MAX_X 1023
#endif
#ifndef CYPHER_TUNE_TOUCH_MIN_Y
#define CYPHER_TUNE_TOUCH_MIN_Y 0
#endif
#ifndef CYPHER_TUNE_TOUCH_MAX_Y
#define CYPHER_TUNE_TOUCH_MAX_Y 599
#endif
// Poll cadence matches the shared GT911 sample throttle; the release
// debounce keeps one dropped sensor frame from ending a press mid-drum.
#ifndef CYPHER_TUNE_TOUCH_POLL_MS
#define CYPHER_TUNE_TOUCH_POLL_MS 8
#endif
#ifndef CYPHER_TUNE_TOUCH_RELEASE_DEBOUNCE_MS
#define CYPHER_TUNE_TOUCH_RELEASE_DEBOUNCE_MS 40
#endif
// Contacts landing within this many px of an unmatched live contact are
// treated as the same finger when GT911 track ids churn.
#ifndef CYPHER_TUNE_TOUCH_MATCH_RADIUS
#define CYPHER_TUNE_TOUCH_MATCH_RADIUS 48
#endif

#endif
