# Cypher Boy (Project 22) Technical Reference

## AI setup prompt

Set up and verify the project at projects/22-cypher-boy.

- Keep rendering on the Adafruit-GFX-style API through Arduino_GFX. Never LVGL.
- Keep every draw and touch path behind `#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)`
  so the headless build stays green.
- Keep serial parity: every touch action must have a serial command equivalent.
- Never commit a game ROM. ROMs are user-supplied and copyrighted.
- Treat the vendored gnuboy core as third-party: local edits go in
  `src/gnuboy/VENDORED.md`, marked in-source with a `CrowPanel patch` comment.

At the end, summarize files changed, commands run, and remaining proof gaps.
Keep README.md user-facing and put implementation detail here.

## Architecture

A thin `.ino` (screen state + serial commands) over five units, each with one
job:

| Unit | Responsibility |
|---|---|
| `src/gnuboy/` | Vendored GPLv2 emulation core (cpu, hw, lcd, sound, tables) |
| `GameBoyHost` | The only TU that includes `gnuboy.h`. ROM load, frame run, pad, saves |
| `GbVideo` | 160x144 RGB565 -> x3 nearest-neighbour blit into the panel viewport |
| `GbInput` | Touch -> Game Boy pad bitfield; MENU edge; serial injection |
| `GbUi` | ROM picker screen + in-game gamepad chrome (`Widgets::`) |
| `GbRomStore` | SD mount, `/roms` listing, `/saves` path derivation |

`GameBoyHost` never draws and `GbUi` never mutates app state; the `.ino` owns
the `Screen` enum and the transitions, so touch and serial drive identical code.

## Screens

- **Picker** — lists `.gb`/`.gbc` from `/roms` as tappable rows, plus a
  "how to play" card and the SD status. Tapping a row launches it.
- **Play** — the live game viewport with the gamepad overlay drawn around it.
  MENU returns to the picker (flushing the battery save on the way out).

## Touch controls

Gameplay controls are sampled as **held regions**, not release edges — you must
be able to hold a direction while pressing A, and multiple controls OR together.
MENU is the exception: it fires on **release**, so dragging off it cancels.

| Control | Rect (x, y, w, h) | Serial twin |
|---|---|---|
| UP | 620, 330, 90, 90 | `button up` |
| DOWN | 620, 510, 90, 90 | `button down` |
| LEFT | 530, 420, 90, 90 | `button left` |
| RIGHT | 710, 420, 90, 90 | `button right` |
| A | 920, 380, 100, 100 | `button a` |
| B | 820, 450, 100, 100 | `button b` |
| SELECT | 620, 250, 130, 56 | `button select` |
| START | 780, 250, 130, 56 | `button start` |
| MENU | 900, 86, 110, 56 | `screen picker` |

These are first-pass values, expected to be tuned on glass. The `touch` command
prints the mapped point to help. `selftest` asserts that every control maps to
exactly one button, that none overlap, that none sit over the viewport, and
that all of them fit inside 1024x600 — an off-panel control is both invisible
and untappable, so that check is not optional.

## Display path

The GB frame is 160x144; the viewport is integer **x3** = 480x432 at (40, 60),
leaving the right side and bottom for the gamepad.

The DSI panel is single-framebuffer, so the frame is composed off-panel then
blitted. It deliberately does **not** use a full-size offscreen canvas: 480x432
is 405 KB, which does not fit the ~300 KB of internal SRAM left (project 08's
252 KB Pong canvas is already near the ceiling), and a PSRAM canvas would mean
reading 405 KB back every frame. Instead each GB scanline is expanded into a
small 480x3 strip in internal SRAM (5.7 KB) and blitted, 144 times per frame,
followed by a single `CrowDisplay::flush()` over the whole viewport rather than
144 small syncs.

`CrowDisplay::begin(..., /*manualFlush=*/true)` so draws only touch the cached
framebuffer until flushed.

## Emulator integration

Vendored from retro-go `retro-core/components/gnuboy/` at commit `4ced120`.
See `src/gnuboy/VENDORED.md` for the file list and every local patch.

Three integration facts that are easy to get wrong (all verified against the
vendored source, not assumed):

1. **PSRAM.** A ROM is 1-2 MB of 16 KB banks and cartridge SRAM is up to 128 KB.
   Those two allocations are patched to `heap_caps_*` with `MALLOC_CAP_SPIRAM`;
   the ESP32-P4 has only ~500 KB internal SRAM. Small hot structures stay
   internal on purpose. `heap_caps` memory frees with plain `free()`, so
   gnuboy's own teardown needs no change.
2. **Silence.** There is no `GB_AUDIO_NONE`. Silence comes from a **NULL audio
   callback** — `gb_sound_emulate()` guards with `if (!output_buf) continue;`
   and only invokes the callback in its overflow branch. Do **not** pass
   `samplerate = 0`: `gb_sound_reset()` computes `(1<<21) / (double)samplerate`
   and casting the resulting infinity to `int` is undefined behaviour. We pass
   `GB_SAMPLERATE` (32000) plus a small throwaway sound buffer.
3. **No video callback needed.** `gnuboy_run(draw)` renders scanlines straight
   into the framebuffer; the callback is only a notification, and gnuboy's
   "draw inside the callback" caveat applies solely to `GB_PIXEL_PALETTED`. We
   use `GB_PIXEL_565_LE` and blit from the loop.

`GameBoyHost.cpp` `static_assert`s our `GbButton` values against gnuboy's
`gb_padbtn_t`, so a future core bump cannot silently swap A and B.

## SD and saves

gnuboy does its own ROM and save file I/O with C stdio, so the card must be
reachable through the FAT VFS — `SD_MMC.begin("/sdcard", ...)` and paths under
`/sdcard`. `GbRomStore` checks `SD_MMC.cardType() != CARD_NONE` before mounting
so it does not fail by re-mounting a card another subsystem already brought up
(the same guard Cypher Desk uses).

Battery save policy: after each frame, if `gnuboy_sram_dirty()` the save is
marked pending; it is flushed **2 s after the last change**, and forced on MENU
and on ROM swap. `gnuboy_load_sram()` restores it at launch (absent file is
fine — a new game simply has no save yet).

> **Library-linkage warning.** `<SD_MMC.h>` is included directly under
> `#if USE_GB_SD`, **not** behind `__has_include`. arduino-cli discovers
> libraries by preprocessing sources and following includes; during that pass
> SD_MMC is not yet on the include path, so an `__has_include` guard evaluates
> false, the library is never linked, and the SD support is silently compiled
> out even with the flag set. Verify linkage by checking that `SD_MMC` appears
> under `<build-path>/libraries/` — a green build alone proves nothing.

## Feature flags

| Flag | Default | Effect |
|---|---|---|
| `USE_DISPLAY` | 0 | Panel bring-up, viewport blit, touch gamepad, picker UI |
| `USE_GB_SD` | 0 | SD_MMC mount, `/roms` listing, `/saves` battery saves |
| `GB_SAMPLERATE` | 32000 | gnuboy mixer rate. Must be non-zero (see above) |
| `GB_SCALE` / `GB_VIEW_X` / `GB_VIEW_Y` | 3 / 40 / 60 | Viewport scale and origin |

With `USE_GB_SD=0` the ROM store reports a placeholder entry and refuses to
"launch" it, so the picker and selftest work on a bare board without pretending
a cartridge exists.

## Build

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_GB_SD=1" ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

`CTAGS_WORKAROUND=1` is required — the local ctags is broken.

## Serial commands

| Command | Does |
|---|---|
| `rom` | List ROMs and their derived save paths |
| `play <n>` | Launch ROM by index |
| `screen picker\|play [n]` | Switch screens |
| `button up+a` / `button none` | Inject a pad state for a frame |
| `save` | Force-write battery SRAM |
| `touch` | Raw + mapped point, taps, held buttons, screen |
| `selftest` | Headless PASS/FAIL/SKIP over the whole flow |
| `status`, `history` | Flags/uptime/heap, recent events |

## selftest

Asserts on real state and reports **SKIP** (never a fake PASS) for anything
needing a card or ROM: host bring-up, framebuffer allocation, run-with-no-ROM
being a safe no-op, ROM store entries and save-path derivation, viewport scale
maths, every control mapping to exactly one button, no control overlaps,
controls clear of the viewport, all controls on-panel, and picker row
hit-testing. With a card present it additionally loads a ROM and asserts frames
advance.

## Proof state

**compile-ready.** All four combos build green for the ESP32-P4 and `SD_MMC`
was confirmed present under `<build-path>/libraries/` for the SD rows.

Not proven, and needing a panel + a legally-obtained ROM:

- a ROM actually booting and rendering
- touch driving gameplay, and the control rects being comfortable in the hand
- frame pacing / perceived speed at x3 scale
- a battery save surviving a power cycle
- GBC colour output on a `.gbc` title
- cartridge RTC behaviour (Pokémon Gold/Silver/Crystal day-night)

## Safety boundary

No ROM is committed or distributed. The emulator is open-source; the games are
not. Bring your own dumps, or use freely-redistributable homebrew for anything
you intend to publish or film.
