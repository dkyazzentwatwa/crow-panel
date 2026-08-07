#ifndef INKWELL_PROJECT_CONFIG_H
#define INKWELL_PROJECT_CONFIG_H

// Inkwell is mock-first. Real SD and display builds pass these flags through
// compiler.cpp.extra_flags so shared translation units see the same values
// (see CLAUDE.md's three-layer flag rule).
#ifndef USE_INKWELL_SD
#define USE_INKWELL_SD 0
#endif

// SD_MMC mount mode: 1-bit bus, matching every other CrowPanel SD consumer
// in this repo (projects 08/09/13/15/18/20/21/22 all default this to 1).
#ifndef INKWELL_SDMMC_1BIT
#define INKWELL_SDMMC_1BIT 1
#endif

// FS-relative paths (SD_MMC prepends the mount point itself -- see
// CLAUDE.md's "Arduino FS prepends the mount point" invariant, and never mix
// these with a C-stdio path that would need the "/sdcard" prefix spelled
// out; this project only ever touches the card through Arduino FS calls).
#ifndef INKWELL_BOOKS_DIR
#define INKWELL_BOOKS_DIR "/books"
#endif
#ifndef INKWELL_CATALOG_DIR
#define INKWELL_CATALOG_DIR "/books/.inkwell"
#endif
#ifndef INKWELL_CATALOG_PATH
#define INKWELL_CATALOG_PATH "/books/.inkwell/catalog.txt"
#endif

// Portrait rotation for Arduino_DSI_Display: 1 = 90° CW (USB at bottom),
// 3 = 90° CCW. Hardware bring-up decides which; default 1 until proven.
#ifndef INKWELL_ROTATION
#define INKWELL_ROTATION 1
#endif

// Logical page geometry (portrait).
#ifndef INKWELL_PAGE_W
#define INKWELL_PAGE_W 600
#endif
#ifndef INKWELL_PAGE_H
#define INKWELL_PAGE_H 1024
#endif

#include <AppConfig.h>

#endif  // INKWELL_PROJECT_CONFIG_H
