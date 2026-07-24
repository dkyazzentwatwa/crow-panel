# Cypher Boy — Game Boy / GBC player (Project 22) — Design

Date: 2026-07-23
Status: approved for planning

## Context

Project 08 (Cypher Gamer Arcade) is a from-scratch touch arcade. The user wants a "wow factor":
play real Game Boy games — specifically touchscreen Pokémon — on the CrowPanel Advanced 7-inch
(ESP32-P4). After reviewing options (retro-go, Peanut-GB, Walnut-CGB), we chose to reuse
**retro-go's `gnuboy` core** (the same emulation code proven across many ESP32 devices) inside a
**new, standalone Arduino project** built with `arduino-cli` — *not* a port of the retro-go
framework itself. The arcade (08) is untouched and remains its own project.

"Use retro-go's core" concretely means: **vendor gnuboy's emulation core and give it our own
host layer** (display, input, storage) built on the shared CrowPanel infrastructure. retro-go's
copy is otherwise wired to its `rg_*` firmware APIs; gnuboy's core is cleanly separable (that is
why it ports everywhere), and it already exposes a clean public API — see "Core API" below.

## Goals (v1 — thin vertical slice)

Boot → pick a ROM from the SD card → **Pokémon running on the panel** → **touch controls**
(D-pad, A, B, Start, Select) → **battery saves persisted to SD** and reloaded on launch. Build
green under `arduino-cli` (baseline + display), headless-safe, with `touch`/`selftest` serial
commands. Prove each rung on the way up before adding the next.

## Non-goals (deferred, in this order after v1 works)

1. **Sound** — GB APU audio through the NS4168 I2S amp. The single biggest added lever (APU +
   I2S DMA timing). v1 runs silent; the video/input/save chain is proven first.
2. Additional cores (NES/nofrendo, SMS, etc.) — v1 is gnuboy only. The host boundary is designed
   so a second core is additive, but no generic multi-core launcher is built yet (YAGNI).
3. Save states (gnuboy supports them), fast-forward, cheats, per-game config.

## Source of truth / honesty boundary

Serial remains the source of truth (native USB-CDC drops once the app runs, and the panel may be
detached). Nothing here is hardware-proven in this session — there is no panel attached. All docs
land at `compile-ready`; "Pokémon boots / touch works / save survives reboot" is on-panel
acceptance the user performs, and a legally-obtained ROM they supply (see Licensing).

## Core API (retro-go `retro-core/components/gnuboy/`)

Files to vendor: `cpu.c/.h`, `gnuboy.c/.h`, `hw.c/.h`, `lcd.c/.h`, `sound.c/.h`, `tables.h`,
plus `COPYING`/`CREDITS`. Public surface we build against (`gnuboy.h`):

- `gnuboy_init(samplerate, audio_fmt, video_fmt, video_cb, audio_cb)` — video_fmt = RGB565.
- `gnuboy_set_framebuffer(void *buffer)` / video callback `void cb(void *buffer)` — frame ready.
- `gnuboy_load_rom(const byte *data, size_t size)` / `gnuboy_load_rom_file(const char *)`.
- `gnuboy_run(bool draw)` — run one frame.
- `gnuboy_set_pad(int)` — button bitfield each frame.
- `gnuboy_sram_dirty()`, `gnuboy_load_sram(file)`, `gnuboy_save_sram(file, quick)` — battery saves.
- `gnuboy_set_hwtype(...)`, `gnuboy_set_palette(...)` — DMG vs GBC.
- `gnuboy_get_time/set_time(...)` — cartridge RTC (Pokémon Gold/Silver/Crystal day-night).

gnuboy is C99; `arduino-cli` compiles `.c` under `src/` and links it with our C++ via
`extern "C"`. Licensing: gnuboy is **GPLv2** — this project folder inherits GPLv2 (standalone;
flagged because the wider suite is mixed-license).

## Architecture

```
projects/22-cypher-boy/
  22-cypher-boy.ino        # thin: setup + loop (run frame -> render -> poll touch -> feed pad -> save)
  config/ProjectConfig.h   # USE_DISPLAY, USE_GB_SD, GB touch calibration + control-layout constants
  README.md  TECHNICAL.md
  src/gnuboy/              # vendored core (cpu, gnuboy, hw, lcd, sound, tables) + COPYING/CREDITS
  src/GameBoyHost.{h,cpp}  # owns the core; begin(), loadRom(), runFrame(), saveIfDirty(), setPad()
  src/GbVideo.{h,cpp}      # 160x144 RGB565 -> internal-SRAM offscreen canvas -> x3 nearest-neighbor blit
  src/GbInput.{h,cpp}      # CrowTouch held-regions -> gnuboy pad bitfield
  src/GbRomStore.{h,cpp}   # SD: list /roms, load ROM -> PSRAM, load/save /saves/<rom>.sav
  src/GbUi.{h,cpp}         # ROM-picker screen + in-game control overlay (Widgets::)
```

The `.ino` mirrors the arcade pattern: a `GameBoyHost` owns the core and exposes discrete methods
the loop and the serial commands both call, so touch and serial stay in parity. Each `src/` unit
has one responsibility and a testable interface:

- **GameBoyHost** — wraps the C core behind a C++ interface; the only translation unit that talks
  to gnuboy. Holds the framebuffer, drives `gnuboy_run`, owns save cadence. Depends on: gnuboy,
  GbRomStore (for save paths).
- **GbVideo** — owns the 160x144 RGB565 offscreen `Arduino_Canvas` in internal SRAM and the
  integer-scaled blit into the centered viewport. Depends on: CrowDisplay canvas. Pure given a
  framebuffer, so testable off-panel by asserting scale math.
- **GbInput** — maps touch (x,y, held) against the control-overlay hitboxes to gnuboy's pad
  bitfield. Depends on: CrowTouch, layout constants. Pure mapping; unit-testable headless.
- **GbRomStore** — SD listing + ROM→PSRAM load + `.sav` load/save. Depends on: SD_MMC. The one
  place that knows the SD layout.
- **GbUi** — ROM picker (list of files, tap to select) and the in-game overlay chrome (D-pad, A/B,
  Start/Select, MENU) drawn with `Widgets::`. Depends on: CrowDisplay, GbRomStore, GbInput layout.

## Display + touch

GB frame is 160x144. Render it into a 160x144 internal-SRAM offscreen canvas (the Pong technique),
then **integer ×3 (480x432) nearest-neighbor** into a centered viewport, leaving a right column
and bottom strip for the `Widgets::` control overlay (D-pad, A, B, Start, Select, MENU). Integer
scaling keeps pixels crisp and the blit cheap; ×4 (640x576) is a later layout option decided on
glass. Touch uses `CrowTouch` **held regions** (not release edges) so directions/buttons can be
held, sampled each frame into the pad bitfield. A MENU control returns to the ROM picker.

## Saves + ROM loading

SD layout: `/roms/*.gb|*.gbc`, `/saves/<rom>.sav`. Load the whole ROM into PSRAM on launch
(Pokémon Red 1 MB, Crystal 2 MB — trivial in 32 MB). Battery save policy: after `gnuboy_run`,
if `gnuboy_sram_dirty()`, mark dirty; flush to `/saves/<rom>.sav` on a debounce timer (a few
seconds idle) and on MENU/quit; reload on launch via `gnuboy_load_sram`. SD access model
(mount SD_MMC's FAT VFS so gnuboy's `*_file` stdio calls work at `/sdcard/...`, vs. memory ROM
load + direct SRAM buffer) is resolved in the plan against what the vendored core exposes; the
memory-load + sram-file path is the default assumption.

## Build + testing

- Builds under the existing `arduino-cli` flow (`scripts/compile-all.sh`, `CTAGS_WORKAROUND=1`).
  Register `projects/22-cypher-boy` in `scripts/project-registry.sh`; add `USE_GB_SD` rows to
  `scripts/check-flag-matrix.sh` (baseline, display, gb-sd, display-gb-sd).
- Headless (`USE_DISPLAY=0`) must compile and run over Serial: the core + GbRomStore + GbInput
  mapping build without the panel behind the usual `#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)`
  guards on draw code.
- `touch` (raw+mapped point, held buttons, current screen) and `selftest` (drive a fixed test ROM
  through N frames asserting the CPU/PC advances and a SRAM save round-trips through GbRomStore)
  serial commands. Serial can also inject pad presses for headless input testing.
- Vertical build order, tested at each rung: **(1)** gnuboy compiles under arduino-cli (baseline);
  **(2)** ROM loads from SD into PSRAM; **(3)** a frame renders to the panel; **(4)** touch drives
  the pad; **(5)** SRAM save persists and reloads. Each rung is the acceptance gate for the next.

## Risks / open items

- **SD access model** for gnuboy's file I/O vs. Arduino SD_MMC — resolved in plan (VFS mount vs.
  memory+buffer). Low risk; both paths are known-workable.
- **arduino-cli + C core linkage** — `.c` in `src/` with `extern "C"`; standard, low risk.
- **DSI blit throughput** at 60 fps — mitigated by integer scaling + internal-SRAM offscreen canvas
  (already proven in project 08). Frame-pace, don't busy-spin.
- **Licensing** — GPLv2 for this project folder; ROMs are user-supplied and legally owned (no ROM
  is committed or distributed; a free homebrew ROM is the recommended demo asset).
- **GBC RTC** (Pokémon G/S/C) — supported via `gnuboy_get/set_time`; wire to the panel clock later,
  not a v1 blocker.
