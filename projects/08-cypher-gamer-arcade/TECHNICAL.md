# CrowPanel Cypher Gamer Arcade Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/08-cypher-gamer-arcade.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/08-cypher-gamer-arcade/TECHNICAL.md.
```

---

Large touch arcade launcher inspired by `cardputer-games`, rebuilt on the shared
`CrowPanelShared` toolkit: the dark "ops" palette, the FreeSans fonts, and the
`Widgets::` primitives (`headerBar`, `tabBar`, `panel`, `touchButton`, `text`).
The default build is offline and Serial-smokeable. When `USE_DISPLAY=1` is
compiled for the CrowPanel ESP32-P4 target, the sketch renders its own
multi-screen touch arcade. Arduino_GFX only; no LVGL.

The three game engines (Pong, Snake, 2048) and their feel are unchanged - only
the chrome, navigation, input, and playfield compositing were modernized.

## Screens

`ArcadeEngine::Screen` selects the top-level view; the pause overlay is a modal
drawn on top of the play screen.

- **Catalog** - `headerBar` ("CYPHER GAMER") + three `panel()` game cards (name,
  tagline, current high score, `PLAY >`) + a bottom `tabBar` `[ARCADE, SCORES]`.
  Tap a card to launch that game.
- **Scores** - `headerBar` ("HIGH SCORES") + three per-game cards showing the
  best score and its persistence source (SD / RAM). Tap a card to play that
  game; the `ARCADE` tab returns to the catalog.
- **Play** - the active game with a `Widgets` header (title + live score) and a
  `PAUSE` button, the game body, and an info rail. Pong's animated field is
  composited into an offscreen canvas and blitted; Snake and 2048 draw directly
  and repaint only when their state changes.
- **Pause overlay** - a centered `panel()` card with four `touchButton`s:
  `RESUME`, `RESTART`, `SCORES`, `QUIT`.

`tick()` returns a typed `ArcadeEngine::UiEvent` (launch/restart/quit/scores) for
each discrete touch action; the sketch executes it through the same engine
methods the serial commands use, so touch and serial stay in parity. Navigation
inside the pause overlay (open/resume) is display-only and has no serial form.

## Touch Controls

| Screen | Gesture | Action |
|---|---|---|
| Catalog | Tap a game card | Launch Pong / Snake / 2048 |
| Catalog | Tap `SCORES` tab | Open the high-score screen |
| Scores | Tap a game card | Launch that game |
| Scores | Tap `ARCADE` tab | Back to the catalog |
| Play (any) | Tap `PAUSE` | Open the pause overlay |
| Play - Pong | Drag inside the field | Move the left paddle |
| Play - Snake | Swipe anywhere | Turn (reverse turns ignored) |
| Play - 2048 | Swipe anywhere | Slide and merge tiles |
| Pause | Tap `RESUME` | Close the overlay, keep playing |
| Pause | Tap `RESTART` | Restart the active game |
| Pause | Tap `SCORES` | Go to the high-score screen |
| Pause | Tap `QUIT` | Return to the catalog |

Navigation and buttons key off the shared `CrowTouch` release edge
(`releasedEdge()` + `releaseX/Y()`), so a drag that starts on one control and
ends elsewhere fires nothing. Only the Pong paddle uses the live `down()` +
`y()` drag position. Swipes are computed from the press-start point to the
release point and are ignored when the press began on a control.

## Serial Commands

115200 baud, line ending Newline. Every command runs the same engine state the
touch UI drives.

- `help` / `status` / `history`
- `catalog` - show the catalog (also the quit-to-home path)
- `play pong` / `play snake` / `play 2048`
- `move up` / `move down` / `move left` / `move right`
- `step` - advance the active game one smoke step
- `reset` - restart the active game (or refresh the catalog)
- `score` - print the current and high scores (same values the scores screen shows)
- `scores` - open the high-score screen (serial mirror of the SCORES tab / pause button)
- `cal` - print the `CROW_TOUCH_*` calibration and the last mapped point
- `touch` - print raw + mapped touch, tap count, and the current screen
- `selftest` - drive the mock flow headlessly with explicit PASS/FAIL lines

`play`, `move`, `step`, `reset`, and `score` exercise the same local state that
touch uses. `selftest` needs no panel - it is the functional check for a bare
board.

## Feature Flags

- `USE_DISPLAY=1`: enables the CrowPanel display/touch path through
  `CrowDisplay` + `CrowTouch`. Rendering and game state stay project-local.
- `USE_SD_HIGHSCORES=1`: attempts SD_MMC high-score persistence at
  `/cypher-gamer-scores.txt`. If the card does not mount, the sketch falls back
  to RAM high scores and reports `sd_ready=0`.
- `ARCADE_SDMMC_1BIT=1`: default SD_MMC mount mode for conservative bring-up.
- Touch calibration uses the shared `CROW_TOUCH_*` flags from `AppConfig.h`
  (`CROW_TOUCH_MIN_X`/`MAX_X`/`MIN_Y`/`MAX_Y`, `CROW_TOUCH_SWAP_XY`,
  `CROW_TOUCH_INVERT_X`/`INVERT_Y`). Override them with `EXTRA_FLAGS` after a
  `touch` diagnostic run if raw touch is rotated, inverted, or clipped.

## Rendering notes

The DSI panel is a single directly-scanned framebuffer, so animating it in place
tears. Pong - the only per-frame animator - composites its 448x288 playfield
into an offscreen `Arduino_Canvas` and blits it once per frame with
`draw16bitRGBBitmap`. The offscreen buffer is allocated in **internal SRAM**
first (`heap_caps_malloc(MALLOC_CAP_INTERNAL)`, ~10x faster than PSRAM for the
recompose + blit) with a PSRAM fallback, and is kept deliberately small rather
than allocating a full-screen 1024x600 buffer in PSRAM (the known-wrong approach
for this panel). `status` reports which pool won (`pong_fb=internal|psram`).
Snake and 2048 change only on a step/move, so they draw directly and repaint on
a `dirty_` flag. The panel is brought up with `manualFlush=true`, so each frame
ends with one `CrowDisplay::flush()` (or a field-region flush during Pong).

## Build

The suite default FQBN and the local ctags workaround:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

SD high-score builds:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_SD_HIGHSCORES=1" ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_HIGHSCORES=1" ./scripts/compile-all.sh
```

All four combinations are rows in `scripts/check-flag-matrix.sh`
(`baseline`, `display`, `sd-highscores`, `display-sd-highscores`) and compile
green.

## Test Flow

1. Compile baseline and display paths (commands above).

2. Serial smoke after upload:

   ```text
   status
   catalog
   play pong
   move up
   step
   score
   play snake
   move right
   step
   play 2048
   move left
   score
   touch
   cal
   ```

3. Headless functional check (no panel needed):

   ```text
   selftest
   ```

   Expect a block of `[selftest] <name> PASS` lines and a final
   `[selftest] summary: N passed, 0 failed`.

4. Touch smoke with `USE_DISPLAY=1`: tap each catalog card, verify the Pong
   drag, Snake swipes, and 2048 swipes; open `PAUSE` and exercise Resume /
   Restart / Scores / Quit; check the `SCORES` tab and its cards.

5. If touch is rotated or clipped, run `touch`, tap corners, read the raw and
   mapped coordinates from Serial, then rebuild with the `CROW_TOUCH_*` flags.

## Proof States

- `compile-ready`: baseline, display, and both SD-high-score builds compile.
  (All four green.)
- `uploaded`: the sketch was flashed to a named serial port.
- `touch-proven`: the real CrowPanel accepted touch across the catalog, all
  three games, the pause overlay, and the scores screen.
- `sd-proven`: `USE_SD_HIGHSCORES=1` mounted SD_MMC and retained scores across a
  reboot.

Do not claim `touch-proven`, `uploaded`, or `sd-proven` from a compile-only run.
Nothing here has been observed on a physical panel yet.
