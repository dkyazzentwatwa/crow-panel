#ifndef CYPHER_GAMER_PROJECT_CONFIG_H
#define CYPHER_GAMER_PROJECT_CONFIG_H

#include <AppConfig.h>

#ifndef USE_SD_HIGHSCORES
#define USE_SD_HIGHSCORES 0
#endif

// GT911 calibration defaults for the 1024x600 CrowPanel surface. Override
// these with EXTRA_FLAGS while hardware-testing if raw touch is rotated,
// inverted, or clipped on a specific board revision.
#ifndef ARCADE_TOUCH_MIN_X
#define ARCADE_TOUCH_MIN_X 0
#endif

#ifndef ARCADE_TOUCH_MAX_X
#define ARCADE_TOUCH_MAX_X 1023
#endif

#ifndef ARCADE_TOUCH_MIN_Y
#define ARCADE_TOUCH_MIN_Y 0
#endif

#ifndef ARCADE_TOUCH_MAX_Y
#define ARCADE_TOUCH_MAX_Y 599
#endif

#ifndef ARCADE_TOUCH_SWAP_XY
#define ARCADE_TOUCH_SWAP_XY 0
#endif

#ifndef ARCADE_TOUCH_INVERT_X
#define ARCADE_TOUCH_INVERT_X 0
#endif

#ifndef ARCADE_TOUCH_INVERT_Y
#define ARCADE_TOUCH_INVERT_Y 0
#endif

#ifndef ARCADE_SDMMC_1BIT
#define ARCADE_SDMMC_1BIT 1
#endif

#endif
