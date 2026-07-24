# Vendored gnuboy core

Source: [ducalex/retro-go](https://github.com/ducalex/retro-go), path
`retro-core/components/gnuboy/`, at commit `4ced120669750ca7228fd0414211430c1d923166`
(fetched 2026-07-23).

License: **GPLv2** — see `COPYING` and `CREDITS`. This makes
`projects/22-cypher-boy/` a GPLv2 folder within the wider suite.

Files taken verbatim: `cpu.c/.h`, `gnuboy.c/.h`, `hw.c/.h`, `lcd.c/.h`,
`sound.c/.h`, `tables.h`, `COPYING`, `CREDITS`.
Deliberately **not** taken: `CMakeLists.txt` (arduino-cli discovers `.c`
automatically), `docs/`, `tests/`.

## Local patches

All local edits are marked in-source with a `CrowPanel patch` comment.

### 1. PSRAM allocation for cartridge memory — `gnuboy.c`

The ESP32-P4 has ~500 KB internal SRAM but 32 MB PSRAM. A Game Boy ROM is
1–2 MB of 16 KB banks and cartridge SRAM is up to 128 KB, so those two
allocations are forced into PSRAM via `heap_caps_*`:

- Added after the includes: `#include "esp_heap_caps.h"` plus the
  `GB_PSRAM_MALLOC` / `GB_PSRAM_CALLOC` macros.
- `gnuboy_load_bank()`: `cart.rombanks[bank] = malloc(BANK_SIZE)`
  → `GB_PSRAM_MALLOC(BANK_SIZE)`.
- `gnuboy_load_rom()`: `cart.rambanks = calloc(cart.ramsize, 0x2000)`
  → `GB_PSRAM_CALLOC(...)`.

Deliberately left in internal SRAM (small and hot, faster there):
`cart.rombanks` pointer table, `hw.bios`, and `hw.rambanks` / `hw.vbanks`
in `hw.c`.

`heap_caps` memory is released by plain `free()` on ESP-IDF, so gnuboy's
existing `free()` calls in `gnuboy_free_rom()` need no change.

## Host integration notes (not patches)

- `gnuboy.h` only defines `LOG_PRINTF` → `printf` when `RETRO_GO` is **not**
  defined. We do not define `RETRO_GO`, so logging goes to the Arduino serial
  console with no host symbol required.
- **Silent v1:** there is no `GB_AUDIO_NONE`. `gb_sound_emulate()` guards with
  `if (!output_buf) continue;` and only invokes the audio callback inside the
  overflow branch, so silence is achieved with a valid samplerate, a small
  throwaway sound buffer, and a **NULL audio callback**. Do NOT pass
  `samplerate = 0` — `gb_sound_reset()` computes
  `(1<<21) / (double)samplerate` and casting the resulting infinity to `int`
  is undefined behavior.
- gnuboy does its own ROM/SRAM file I/O with C stdio (`fopen`/`fread`), so the
  SD card must be reachable through the FAT VFS (paths under `/sdcard`).
