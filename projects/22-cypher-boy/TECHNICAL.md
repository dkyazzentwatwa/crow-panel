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

A thin `.ino` (screen state + serial commands) over six units, each with one
job:

| Unit | Responsibility |
|---|---|
| `GbAudio` | GB APU -> NS4168 I2S amp (IDF `i2s_std`), volume/mute, fail-soft |
| `src/gnuboy/` | Vendored GPLv2 emulation core (cpu, hw, lcd, sound, tables) |
| `GameBoyHost` | The only TU that includes `gnuboy.h`. ROM load, frame run, pad, saves |
| `GbVideo` | 160x144 RGB565 -> x3 nearest-neighbour blit into the panel viewport |
| `GbInput` | Touch -> Game Boy pad bitfield; MENU edge; serial injection |
| `GbUi` | Picker, gamepad chrome, pause overlay (`Widgets::`) |
| `GbRomStore` | SD mount, `/roms` listing, `/saves` + `/states` path derivation |

`GameBoyHost` never draws and `GbUi` never mutates app state; the `.ino` owns
the `Screen` enum and the transitions, so touch and serial drive identical code.

## Screens

- **Picker** — lists `.gb`/`.gbc` from `/roms` as tappable rows, plus a
  "how to play" card and the SD status. Tapping a row launches it.
- **Play** — the live game viewport with the gamepad drawn around it.
- **Pause overlay** — modal over Play, opened by MENU. Resume, save/load state
  (3 slots), fast-forward, sound, quit to list. Gameplay is frozen while it is
  open and only overlay taps are read.

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
| MENU | 900, 86, 110, 56 | `pause` (opens the overlay; `screen picker` quits) |

These are first-pass values, expected to be tuned on glass. The `touch` command
prints the mapped point to help. `selftest` asserts that every control maps to
exactly one button, that none overlap, that none sit over the viewport, and
that all of them fit inside 1024x600 — an off-panel control is both invisible
and untappable, so that check is not optional.

## Display path

The GB frame is 160x144; the viewport is integer **x3** = 480x432 at (40, 84),
leaving the right side for the gamepad. `GB_VIEW_Y` must clear
`Widgets::kChromeHeaderH` (72) plus the 6px viewport frame, or the game picture
covers the header subtitle.

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

## Audio

Enabled with `USE_GB_AUDIO=1`. Follows the path proven by project 09:

- IDF `driver/i2s_std.h` directly, **not** the Arduino `ESP_I2S` wrapper, which
  hardcodes a ~65 ms DMA ring - far too much latency for something you press
  buttons at. Here it is 4 x 256 frames (~32 ms at 32 kHz).
- **Stream silence before raising the amp enable**, so the amp wakes onto a
  clean bus instead of popping.
- Amp enable is IO30 and **ACTIVE-LOW** on this panel; the polarity comes from
  `HardwareProfile.audio.controlActiveHigh`, never hardcoded.
- gnuboy is initialised `GB_AUDIO_STEREO_S16` so its mixer emits interleaved
  stereo that feeds I2S with no conversion.
- The write from gnuboy's audio callback uses a **bounded 50 ms timeout**: that
  paces the emulator to real time when audio is the bottleneck, but can never
  hang the app if I2S stalls - the block is dropped and counted as an underrun
  (`audio` prints the count).

Fail-soft throughout: any failure leaves `ready()` false, nothing is written,
and the game keeps running silently.

## Save states and fast-forward

`gnuboy_save_state()` / `gnuboy_load_state()` were already in the vendored core,
so slots are UI work only. Three slots per game at `/states/<rom>.st<n>`.

A save state is a **full machine snapshot**; the battery save (`.sav`) is only
cartridge SRAM. They are independent - a save state does not write the `.sav`.

MENU opens a **pause overlay** rather than quitting outright, so a stray tap can
never dump you out of a game: resume / save state / load state / fast-forward /
sound / quit, plus slot pills. Fast-forward runs `kFastForwardFrames` (3)
emulated frames per drawn frame - emulation is cheap, the blit is what costs.

## Feature flags

| Flag | Default | Effect |
|---|---|---|
| `USE_DISPLAY` | 0 | Panel bring-up, viewport blit, touch gamepad, picker UI |
| `USE_GB_SD` | 0 | SD_MMC mount, `/roms` listing, `/saves` + `/states` |
| `USE_GB_AUDIO` | 0 | Sound out of the NS4168 I2S amp |
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

**field-proven, 2026-07-24.** Pokemon Blue loads from SD and plays on the panel
with the touch gamepad and audible sound. Confirmed on hardware in the same
session: the animated boot splash and chime, all five themes, the centred
gamepad layout, save-state slots with screenshot thumbnails, fast-forward, the
volume/brightness steppers, idle backlight dimming, and the play-time library
sort.

The 2026-07-23 bring-up is what surfaced the SD path-namespace bug (see the
library-linkage warning above) — the card mounted fine and every FS call was
silently looking at `/sdcard/sdcard/...`.

All six flag combos compile-ready, with library linkage verified under
`<build-path>/libraries/` rather than inferred from a green build.

Still unexercised, and honestly so:

- **GBC colour** on a real `.gbc` title (auto-detected from the cart header, no
  flag needed — simply untested)
- **cartridge RTC** behaviour (Pokemon Gold/Silver/Crystal day-night). gnuboy
  maps cart type 16 to MBC3 with 64 KB RAM and sets `has_rtc`, i.e. MBC30, so
  Crystal is expected to work — but expectation is not evidence
- **a battery save surviving a full power cycle** (saves are written and
  reloaded within a session; the cold-boot path has not been watched)
- frame-rate headroom with audio enabled under a demanding title

## Safety boundary

No ROM is committed or distributed. The emulator is open-source; the games are
not. Bring your own dumps, or use freely-redistributable homebrew for anything
you intend to publish or film.
