# Vendored gwenesis (Sega Genesis / Mega Drive)

Source: [ducalex/retro-go](https://github.com/ducalex/retro-go), path
`gwenesis/components/gwenesis/src/`, at commit
`4ced120669750ca7228fd0414211430c1d923166` (fetched 2026-07-24).

License: **GPLv2**, same as gnuboy — no new licensing question for this folder.

Gated behind `USE_GENESIS_CORE` (default 0). **Not yet integrated** — the core is
vendored and compiles, but nothing calls it; `GenesisCore` is Task 5.

## Measured cost (2026-07-24)

| | |
|---|---|
| Flash (text+data) | **553 KB** — `m68kcpu` alone is 460 KB, `Z80` 64 KB |
| RAM (bss) | **130 KB** — `gwenesis_bus` 72 KB, `ym2612` 50 KB |
| App partition | 3.0 MB, currently 410 KB used, **2.66 MB free** |

Comfortably fits. No partition change needed: `huge_app` is also 3 MB, and the
larger schemes (`app5M_*`, `app13M_*`) assume 32 MB flash — this board has 16 MB
(the "32" in ESP32-P4NRW32 is PSRAM).

The 130 KB of bss is the number to watch, not flash: internal SRAM is the scarce
resource here. Genesis's 143 KB framebuffer must therefore go to **PSRAM**.

## What was taken

31 files: `bus/`, `cpus/M68K/`, `cpus/Z80/`, `io/`, `savestate/`, `sound/`,
`vdp/`.

**Deliberately skipped:**
- `m68ki_instruction_jump_table_full.h` (1.25 MB) and `m68ki_cycles_full.h`
  (475 KB) — the non-`_full` variants are used instead.
- `Z80.o` — a prebuilt object checked into upstream.
- `ConDebug.c`, `Debug.c` — Z80 disassembly/debug, not needed on device.
- `readme.txt`, `docs/`.

## Local patches

### 1. Flattened directory tree

Upstream relies on CMake adding every subdirectory to the include path
(`COMPONENT_ADD_INCLUDEDIRS`), so its sources use bare includes like
`#include "m68k.h"` while the file lives in `cpus/M68K/`. arduino-cli adds no
such paths, so all 31 files were flattened into this single directory, where
quoted includes resolve against the including file's own directory. Checked for
basename collisions first — there were none, and gnuboy stays in its own
directory so its `tables.h` cannot clash with this core's `Tables.h` on a
case-insensitive filesystem.

### 2. Every `.c` gated with `#if USE_GENESIS_CORE`

arduino-cli compiles **every** `.c` under `src/` regardless of feature flags, so
each file is wrapped whole. An undefined macro evaluates to 0 in `#if`, so a
build without the flag pays nothing — verified: the default build is byte-identical
(419 640 -> 419 634, a 6-byte string difference) with the tree present.

## Build requirements (both are mandatory)

```
--build-property "compiler.cpp.extra_flags=-DUSE_GENESIS_CORE=1"
--build-property "compiler.c.extra_flags=-DUSE_GENESIS_CORE=1 -Wno-incompatible-pointer-types -Wno-error=incompatible-pointer-types"
```

1. **`compiler.c.extra_flags` is required and easy to miss.**
   `compiler.cpp.extra_flags` does **not** reach `.c` files. Passing only the cpp
   form leaves `USE_GENESIS_CORE` undefined for every vendored source, so all ten
   objects compile to **zero bytes** and the build still goes green — a silent
   no-op core. Verified by inspecting `compile_commands.json`: the flag was
   present on `GbVideo.cpp` and absent on `m68kcpu.c`. Same failure family as the
   `__has_include` linkage trap; check object sizes, not exit status.
   *(No other project in the suite has flag-dependent `.c` files, so none were
   affected — checked.)*
2. **`-Wno-incompatible-pointer-types`** is what upstream itself uses
   (`rg_setup_compile_options(-O2 -Wno-incompatible-pointer-types)` in
   gwenesis's CMakeLists). gwenesis carries both a "host" and an "embedded"
   framebuffer path with mismatched pointer types; GCC 14 promoted that warning
   to an error by default, which is why it blocks here and not upstream.

## Host-side change this forced

gwenesis exports a global named `screen`. The sketch had `Screen screen` and the
two collided at link time, so ours was renamed **`uiScreen`** — `screen` was too
generic for a global regardless.

## CRITICAL corrections (2026-07-24, after the first on-hardware test)

The first Genesis bring-up rebooted ~2 s into every game. Two vendoring mistakes:

### The `_full` M68K tables are REQUIRED, not optional

The initial vendoring kept `m68ki_instruction_jump_table.h` / `m68ki_cycles.h` and
skipped the `_full` variants to save flash. **That was wrong.** m68kcpu.c dispatches
every instruction with the *unmasked* 16-bit opcode (`m68ki_instruction_jump_table[REG_IR]`,
`REG_IR` ∈ 0x0000..0xFFFF), so the tables must have all 65536 entries. The non-`_full`
tables have only **61376 (0xEFC0)** — any opcode ≥ 0xEFC0 (line-F traps, some line-E,
illegal encodings) called a function pointer past the end of the array → wild jump →
reboot a few seconds into a game. The `_full` tables (65536 entries) are now vendored,
`TABLES_FULL` is `#define`d in `m68kconf.h` (which is included before m68kops.h in
m68kcpu.c, so its `#ifdef TABLES_FULL` line-F handler `m68k_op_1111` compiles), and the
two include sites were made unconditional. The non-`_full` tables were deleted. Net flash
cost of the complete tables vs the broken ones: ~+20 KB.

### VRAM backing is host-provided

`gwenesis_vdp_mem.c` declares `unsigned char *VRAM;` and only assigns it in the
Game&Watch target branch (which we don't compile), so in this build it was an unassigned
pointer that `gwenesis_vdp_reset()` memsets and every tile/sprite fetch reads through.
`GenesisCore::begin()` now allocates 64 KB (internal SRAM, PSRAM fallback) and assigns
`VRAM` before any ROM loads.
