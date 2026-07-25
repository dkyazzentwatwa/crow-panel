# Vendored nofrendo (Nintendo Entertainment System)

Source: [ducalex/retro-go](https://github.com/ducalex/retro-go), path
`retro-core/components/nofrendo/`, at commit
`4ced120669750ca7228fd0414211430c1d923166` (fetched 2026-07-24) — the same commit
as the vendored gwenesis, on `master` (**not** `dev`; the two differ).

License: **GPLv2**, same as gnuboy and gwenesis. No new licensing question.

Gated behind `USE_NES_CORE` (default 0).

## The headline: this core is FRAME-DRIVEN, not blocking

The multi-system design originally assumed nofrendo was caller-style — that
`nofrendo_start()` blocks and calls host callbacks, so NES would need its own
FreeRTOS task with cross-task frame marshalling. **That was wrong**, and the
correction is what makes this port easy:

```c
// nes/nes.h:122
void nes_emulate(bool draw);          // runs EXACTLY ONE frame, then returns
```

`nofrendo_start()` does exist and does block (`nofrendo.c:117-122`), which is
where the belief came from — and *classic/Espressif* nofrendo genuinely is
caller-style with a full `osd_*` layer. But ducalex stripped that layer, and
retro-go's own NES app never calls `nofrendo_start`: `retro-core/main/main_nes.c`
runs its own `while (true)` loop calling `nes_emulate(drawFrame)`. That dead
wrapper is simply not vendored.

So `nes_emulate(bool draw)` is a literal signature match for
`EmuCore::runFrame(bool draw)` — same shape as gnuboy and gwenesis.

## Measured cost (2026-07-24)

| | |
|---|---|
| Flash (text+data) | **126 KB** — `rom.c` 52 KB (mostly `database.h` rodata), `cpu.c` 32 KB |
| RAM (bss) | **5 KB** — far smaller than gwenesis's 130 KB |
| Framebuffer | 65,536 B in **PSRAM** (see pitch note below) |
| Default build | **byte-identical** with the tree present (423,528 b), so the gating is proven inert |

Much cheaper than Genesis on both axes. GB + Genesis + NES fit the 3 MB app
partition comfortably.

## What was taken

83 files: `nes/` (20), `mappers/` (59 + `mappers.h`), and root `config.h`,
`database.h`, `palettes.h`, `nofrendo.c`, `nofrendo.h`.

**Deliberately skipped:**
- `docs/` — 7.7 MB, including a 6.9 MB test-ROM zip
- `nes/dis.c` + `nes/dis.h` — 6502 disassembler, only reachable via
  `NES6502_DISASM` which is commented out in `config.h`
- `COPYING`, `CREDITS` — licence text recorded here instead
- `CMakeLists.txt` — arduino-cli discovers `.c` automatically

No prebuilt `.o` files (gwenesis shipped a `Z80.o`) and **no `_full`/variant
table duplicates anywhere** — the trap that broke the Genesis port does not exist
here. Every indexed table was checked against its index range: `opcode_table[256]`
is dense over a `uint8`; `vbl_lut[32]`, `duty_flip[4]`, `freq_limit[8]`,
`noise_freq[16]`, `dmc_clocks[16]` all match their masks; the mapper registry and
`database.h` are NULL/sentinel-terminated **linear searches**, not index tables.

## Local patches

### 1. Flattened tree + include rewrites

Everything lives in this one directory (arduino-cli adds no include dirs). Unlike
gwenesis — where flattening *fixed* includes — nofrendo has **57 path-qualified
includes that flattening breaks**, all rewritten to bare filenames:
`"nes/nes.h"` → `"nes.h"`, `"mappers/mappers.h"` → `"mappers.h"`,
`"../database.h"` → `"database.h"`.

**Basename collisions:** none within nofrendo, none against `src/gwenesis/`.
`cpu.c`/`cpu.h` DO collide with `src/gnuboy/` — harmless, because each core stays
in its own directory and quoted includes resolve against the including file's own
directory. **Never add a broad `-I` covering both**, or `#include "cpu.h"` would
silently resolve to the wrong core.

### 2. Every `.c` gated with `#if USE_NES_CORE`

arduino-cli compiles every `.c` under `src/` regardless of flags, so all 68 are
wrapped whole. Verified: the default build is byte-identical with the tree present.

### 3. `utils.h` — guard `IRAM_ATTR`

Upstream has a bare `#define IRAM_ATTR` (empty) in the non-`RETRO_GO` branch.
`nes.h` includes `utils.h`, so any C++ TU seeing both `Arduino.h` and `nes.h`
would hit a macro redefinition against `esp_attr.h`. Now `#ifndef`-guarded.
Consequence: `nes6502_execute` runs from flash cache rather than IRAM — a
deliberate later optimisation, not a correctness issue.

### 4. `utils.h` — wire a real CRC32

Upstream stubs `#define CRC32(a,b,c) (0)` outside `RETRO_GO`, which makes
`rom.checksum` 0 so the 4,942-entry `database.h` can **never** match — paying
~49 KB of rodata for a table searched against a constant, and losing region
detection plus mapper/mirroring corrections for mis-headered dumps. Now uses
`esp_rom_crc32_le()`, the same reflected-0xEDB88320 CRC retro-go uses via
`rg_crc32`.

**Do NOT define `RETRO_GO`** — it pulls in `<rg_system.h>` and the whole retro-go
component. The standalone `#else` branch plus these two patches is what we want.

## Build requirements

```
--build-property "compiler.cpp.extra_flags=-DUSE_NES_CORE=1"
--build-property "compiler.c.extra_flags=-DUSE_NES_CORE=1 -DNES6502_JUMPTABLE \
    -Wno-array-bounds -Wno-error=format -Wno-format \
    -Wno-incompatible-pointer-types -Wno-error=incompatible-pointer-types"
```

- **`compiler.c.extra_flags` is mandatory** — 100% of nofrendo is `.c`, and
  `compiler.cpp.extra_flags` reaches none of it. Without it every object compiles
  to zero bytes and the build still goes green. Verify by object size, not exit
  status: `riscv32-esp-elf-size <build>/sketch/src/nofrendo/*.o`.
- `-Wno-array-bounds -Wno-error=format -Wno-format` — what upstream's own
  CMakeLists sets.
- **`-DNES6502_JUMPTABLE`** matters more here than upstream realised: Arduino's
  ESP32-P4 flags include `-fno-jump-tables -fno-tree-switch-conversion`, so the
  256-case switch nofrendo relies on degrades to a compare tree. This define
  selects the explicit computed-`goto` path instead. Costs ~11 KB, worth it.

## Host obligations — refreshingly few

**No host-provided globals or callbacks.** Unlike gwenesis (which needed
`system_clock`, `scan_line`, audio buffers and an input hook), nofrendo keeps all
state file-`static`. `nes.blit_func` is the only function pointer and it is
optional (`nes.c:75` guards `if (draw && nes.blit_func)`) — leave it NULL and read
the framebuffer directly.

**One pointer to allocate:** `nes.vidbuf`, 65,536 B, installed via
`nes_setvidbuf()`.

### Four traps the integration must handle

1. **`nes_setvidbuf()` must be re-armed EVERY frame.** `nes_reset()` sets
   `nes.vidbuf = NULL` (`nes.c:203`) and `nes_insertcart()` calls `nes_reset(true)`
   — then `nes_emulate` silently does `draw = draw && nes.vidbuf != NULL`, giving a
   permanently black screen with **no error**. This is the analogue of gwenesis's
   unassigned `VRAM`, except it fails soft, which is harder to spot. retro-go
   re-arms it every frame; so do we.
2. **`nes.mapper` / `nes.cart` are dereferenced unguarded** (`nes.c:51`, `:190`).
   Calling `runFrame()` before a successful cart insert is a null-deref reboot.
   Early-return on `!romLoaded_`.
3. **The first `nes_emulate()` after a load is a PARTIAL frame** — `nes_reset()`
   leaves `nes.scanline = 241`, so it runs only lines 241-261. Two throwaway
   `nes_emulate(false)` calls in `start()` are non-negotiable.
4. **Pitch is 272, not 256.** `NES_SCREEN_PITCH = 8 + 256 + 8` — there are 8
   pixels of overdraw each side and the renderer uses them. Verified worst case:
   sprites reach byte 270, background reaches byte 1 (it never underflows, unlike
   gwenesis). A 256-wide buffer corrupts memory every frame. `EmuCore::stride()`
   returns 272 while `frameW()` returns 256.

## Output format

8-bit palette indices plus a 256-entry RGB565 LUT from
`nofrendo_buildpalette(NES_PALETTE_PVM, 16)` (host owns the `free()`). That is
exactly the `framebuffer8()` + `palette()` path already built for Genesis. The LUT
is native little-endian RGB565 — **do not byte-swap it** (the mistake that gave
Genesis a blue cast).
