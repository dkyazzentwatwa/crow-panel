#ifndef CYPHER_TUNE_MPC_PROJECT_CONFIG_H
#define CYPHER_TUNE_MPC_PROJECT_CONFIG_H

#include <AppConfig.h>

// Engine sample rate. All synthesis/mixing happens at this rate; SD WAVs and
// backing loops may be any rate 8-48 kHz (the voice resampler converts on the
// fly).
//
// 32000, not 22050: an 11 kHz Nyquist makes hats and cymbals dull and is far
// too low for the oscillator work this engine is growing into. The builtin kit
// and every shipped loop pack stay at 22050 and are resampled up, which is
// also the regression test that the resampler is correct.
#ifndef CYPHER_TUNE_ENGINE_RATE
#define CYPHER_TUNE_ENGINE_RATE 32000
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
// BLOCK frames of queued audio. Raise DESC if the underrun counter moves.
//
// DESC is 6, not 4, because the rate went up: 4 * 128 @ 32000 Hz is a 16 ms
// ring, which throws away 31% of the underrun margin the 22050 build was
// proven at. 6 * 128 @ 32000 = 24 ms, matching the old 23 ms, with a ~28 ms
// worst-case pad-to-speaker. BLOCK stays 128 - it keeps step-boundary
// splitting fine-grained and leaves acc_/out_ unchanged.
#ifndef CYPHER_TUNE_BLOCK_FRAMES
#define CYPHER_TUNE_BLOCK_FRAMES 128
#endif
#ifndef CYPHER_TUNE_DMA_DESC
#define CYPHER_TUNE_DMA_DESC 6
#endif

#ifndef CYPHER_TUNE_VOICES
#define CYPHER_TUNE_VOICES 8
#endif

// Per-pad length cap in SOURCE frames, applied by WavLoader as the file is
// read at its own rate and BEFORE any resampling - so this is a per-pad PSRAM
// budget (551 KB), not a duration at the engine rate. Changing the engine rate
// does not affect it.
//
// 275625 is 12.5 s of 22050 Hz source. Kits mix one-shots with whole loops
// played as pad chops and the longest of those is 12.29 s, so capping shorter
// would cut a 4-bar loop mid-phrase. A realistic mixed kit lands near 3.5 MB
// of PSRAM; the pathological all-pads-maxed case is 8.8 MB per bank, and there
// are two banks so an SD kit can stage while the other plays.
#ifndef CYPHER_TUNE_MAX_SAMPLE_FRAMES
#define CYPHER_TUNE_MAX_SAMPLE_FRAMES 275625
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
