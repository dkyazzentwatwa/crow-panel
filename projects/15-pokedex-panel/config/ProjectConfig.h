#ifndef POKEDEX_PANEL_PROJECT_CONFIG_H
#define POKEDEX_PANEL_PROJECT_CONFIG_H

#include <AppConfig.h>

// Use the original esp32-pokedex SD card layout when this is enabled:
//   /pokemon/index.csv
//   /pokemon/catalog_meta.json
//   /pokemon/data/*.json
// Project 15 is SD-first. A missing card or incomplete catalog falls back to
// the built-in entries with a visible reason, so the demo remains usable.
#ifndef USE_SD_POKEDEX
#define USE_SD_POKEDEX 1
#endif

#ifndef POKEDEX_SDMMC_1BIT
#define POKEDEX_SDMMC_1BIT 1
#endif

#ifndef POKEDEX_INDEX_PATH
#define POKEDEX_INDEX_PATH "/pokemon/index.csv"
#endif

#ifndef POKEDEX_DATA_DIR
#define POKEDEX_DATA_DIR "/pokemon/data/"
#endif

#ifndef POKEDEX_META_PATH
#define POKEDEX_META_PATH "/pokemon/catalog_meta.json"
#endif

// Sprites live beside the catalog on the same card. 40x40 24-bit BMP each.
#ifndef USE_POKEDEX_SPRITES
#define USE_POKEDEX_SPRITES 1
#endif

#ifndef POKEDEX_SPRITE_DIR
#define POKEDEX_SPRITE_DIR "/pokemon/sprites/"
#endif

// Grid tiles are 40x40 at 2x. The detail hero is 40x40 at 7x (280px): the
// hero panel's vertical budget above the ATK/DEF/HP stat bars is ~280px
// (kHeroY+8 to kHeroY+288 in PokedexDashboard::drawDetail), and
// draw16bitRGBBitmapWithTranColor cannot rescale a bitmap - it blits pixels
// 1:1 - so the cached resolution has to fit that budget exactly. 8x (320px)
// was tried first and measured against the real sprite pack: 315 of 1573
// sprites have art reaching into what would be the cropped-off bottom 6
// native rows, several all the way to the sprite's last row, so cropping
// visibly chopped off feet/tails on a fifth of the pack. 7x fits without
// cropping anything.
#ifndef POKEDEX_SPRITE_TILE_SCALE
#define POKEDEX_SPRITE_TILE_SCALE 2
#endif

#ifndef POKEDEX_SPRITE_HERO_SCALE
#define POKEDEX_SPRITE_HERO_SCALE 7
#endif

// One page of tiles, so a full grid repaint never evicts a tile it still needs.
#ifndef POKEDEX_SPRITE_CACHE_SLOTS
#define POKEDEX_SPRITE_CACHE_SLOTS 18
#endif

// GT911 touch calibration defaults for the CrowPanel 1024x600 surface. These
// mirror the Arcade project so the Pokedex panel can be tuned with EXTRA_FLAGS
// if a board revision reports raw touch in a different orientation.
#ifndef POKEDEX_TOUCH_MIN_X
#define POKEDEX_TOUCH_MIN_X 0
#endif

#ifndef POKEDEX_TOUCH_MAX_X
#define POKEDEX_TOUCH_MAX_X 1023
#endif

#ifndef POKEDEX_TOUCH_MIN_Y
#define POKEDEX_TOUCH_MIN_Y 0
#endif

#ifndef POKEDEX_TOUCH_MAX_Y
#define POKEDEX_TOUCH_MAX_Y 599
#endif

#ifndef POKEDEX_TOUCH_SWAP_XY
#define POKEDEX_TOUCH_SWAP_XY 0
#endif

#ifndef POKEDEX_TOUCH_INVERT_X
#define POKEDEX_TOUCH_INVERT_X 0
#endif

#ifndef POKEDEX_TOUCH_INVERT_Y
#define POKEDEX_TOUCH_INVERT_Y 0
#endif

// During bring-up, try common raw GT911 orientations if the configured mapping
// misses every button/row. Disable once the exact mapping is proven.
#ifndef POKEDEX_TOUCH_AUTO_REMAP
#define POKEDEX_TOUCH_AUTO_REMAP 1
#endif

#endif
