#ifndef CYPHER_BOY_PROJECT_CONFIG_H
#define CYPHER_BOY_PROJECT_CONFIG_H

// Cypher Boy - Game Boy / GBC player for the CrowPanel Advanced 7-inch.
// Mock-first like the rest of the suite: with USE_GB_SD=0 the ROM store
// reports a built-in placeholder list so every screen and the selftest work
// with no SD card and no ROM present.

#ifndef USE_DISPLAY
#define USE_DISPLAY 0
#endif

// SD-backed ROM/save storage. Off by default so the baseline build needs no
// card. gnuboy does its own stdio file I/O, so the card is mounted through the
// FAT VFS and reached at GB_SD_ROOT.
#ifndef USE_GB_SD
#define USE_GB_SD 0
#endif

// Game Boy APU sound out of the NS4168 I2S amp. Off by default so the baseline
// build stays silent and card-free. Fails soft: if I2S will not come up the
// emulator keeps running with no sound.
#ifndef USE_GB_AUDIO
#define USE_GB_AUDIO 0
#endif

// --- Game Boy frame geometry -----------------------------------------------
// The GB frame is 160x144. It is composited into an internal-SRAM offscreen
// canvas and blitted with integer nearest-neighbour scaling: integer scale
// keeps pixels crisp and the blit cheap. x3 = 480x432, which leaves the right
// side and bottom of the 1024x600 panel free for the touch gamepad.
#define GB_W 160
#define GB_H 144
#define GB_SCALE 3
// Centred horizontally: the gamepad sits in the side zones either side of the
// screen, the way a real handheld is laid out.
#define GB_VIEW_X ((1024 - GB_W * GB_SCALE) / 2)
// Must clear Widgets::kChromeHeaderH (72) plus the 6px viewport frame, or the
// game picture covers the header subtitle. 84 - 6 = 78 > 72.
#define GB_VIEW_Y 84

// --- SD layout --------------------------------------------------------------
// TWO PATH NAMESPACES - these are not interchangeable:
//
//   *_FS  : for the Arduino FS API (SD_MMC.open/exists/mkdir). These must NOT
//           include the mount point, because FS/vfs_api.cpp prepends it
//           ("%s%s", _mountpoint, path). Passing "/sdcard/roms" here resolves
//           to "/sdcard/sdcard/roms" and silently fails.
//   plain : full VFS paths for C stdio (fopen), which is what gnuboy uses
//           internally for ROM and battery-save files. These DO include the
//           mount point.
//
// On the card itself both refer to the same folders at the root: roms/ saves/.
#define GB_SD_ROOT     "/sdcard"
#define GB_ROM_DIR_FS  "/roms"
#define GB_SAVE_DIR_FS "/saves"
#define GB_STATE_DIR_FS "/states"
#define GB_ROM_DIR     GB_SD_ROOT GB_ROM_DIR_FS   // gnuboy stdio
#define GB_SAVE_DIR    GB_SD_ROOT GB_SAVE_DIR_FS  // gnuboy stdio
#define GB_STATE_DIR   GB_SD_ROOT GB_STATE_DIR_FS // gnuboy stdio
#define GB_STATS_PATH  GB_SD_ROOT "/playtime.csv"  // stdio

// Idle backlight. Generous by default so it never dims mid-cutscene; any touch
// or serial command restores full brightness.
#ifndef GB_IDLE_DIM_MS
#define GB_IDLE_DIM_MS 90000
#endif

// Save-state slots (Delta-style). Slot files are <rom>.st0 .. .st<N-1>.
#ifndef GB_STATE_SLOTS
#define GB_STATE_SLOTS 3
#endif

// --- Audio ------------------------------------------------------------------
// gnuboy's mixer rate. With USE_GB_AUDIO=0 the audio callback is NULL so the
// mixer runs but emits nothing. Do NOT set this to 0 - gb_sound_reset()
// divides by it and casting the resulting infinity to int is UB.
// See src/gnuboy/VENDORED.md.
#ifndef GB_SAMPLERATE
#define GB_SAMPLERATE 32000
#endif

#endif
