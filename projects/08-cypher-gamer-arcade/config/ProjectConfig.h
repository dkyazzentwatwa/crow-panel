#ifndef CYPHER_GAMER_PROJECT_CONFIG_H
#define CYPHER_GAMER_PROJECT_CONFIG_H

#include <AppConfig.h>

// Optional SD_MMC high-score persistence. Off by default so the baseline and
// display builds stay green without a card; the engine falls back to RAM scores
// and reports sd_ready=0 when the mount fails.
#ifndef USE_SD_HIGHSCORES
#define USE_SD_HIGHSCORES 0
#endif

// Default SD_MMC mount mode for conservative bring-up (1-bit bus).
#ifndef ARCADE_SDMMC_1BIT
#define ARCADE_SDMMC_1BIT 1
#endif

// Touch calibration is handled by the shared CrowTouch helper. Override the
// panel's raw GT911 range or axis flips with the shared CROW_TOUCH_* flags from
// AppConfig.h (CROW_TOUCH_MIN_X/MAX_X/MIN_Y/MAX_Y, CROW_TOUCH_SWAP_XY,
// CROW_TOUCH_INVERT_X/INVERT_Y) via EXTRA_FLAGS while hardware-testing. The
// `touch` and `cal` serial commands print the live raw and mapped points.

#endif
