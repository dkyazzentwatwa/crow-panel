# CrowPanel Cypher Gamer Arcade

Large touch arcade launcher inspired by `cardputer-games`.

V1 includes a project-local game engine for Pong, Snake, and 2048. The default
build is offline and Serial-smokeable. When `USE_DISPLAY=1` is compiled for the
CrowPanel ESP32-P4 target, the sketch renders its own touch arcade surface.

## Touch Controls

- Menu: tap `PONG`, `SNAKE`, or `2048`.
- Pong: drag inside the playfield to move the left paddle.
- Snake: swipe anywhere to turn. Reverse turns are ignored.
- 2048: swipe anywhere to slide and merge tiles.
- In-game buttons: `MENU` returns to the catalog; `RESTART` resets the active
  game.

## Serial Commands

- `help` / `status` / `history`
- `catalog`
- `play pong`
- `play snake`
- `play 2048`
- `move up`
- `move down`
- `move left`
- `move right`
- `step`
- `reset`
- `score`
- `cal`

Serial remains the smoke path even without a display. `play`, `move`, `step`,
`reset`, and `score` exercise the same local state that touch uses.

## Feature Flags

- `USE_DISPLAY=1`: enables the CrowPanel display/touch path through
  `CrowDisplay`. Rendering and game state stay project-local.
- `USE_SD_HIGHSCORES=1`: attempts SD_MMC high-score persistence at
  `/cypher-gamer-scores.txt`. If the card does not mount, the sketch falls back
  to RAM high scores and reports `sd_ready=0`.
- `ARCADE_SDMMC_1BIT=1`: default SD_MMC mount mode for conservative bring-up.
- `ARCADE_TOUCH_MIN_X`, `ARCADE_TOUCH_MAX_X`, `ARCADE_TOUCH_MIN_Y`,
  `ARCADE_TOUCH_MAX_Y`: raw GT911 range mapping.
- `ARCADE_TOUCH_SWAP_XY=1`: swaps raw touch axes before mapping.
- `ARCADE_TOUCH_INVERT_X=1` / `ARCADE_TOUCH_INVERT_Y=1`: flips mapped axes.

Example display build:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

Example SD high-score build:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_HIGHSCORES=1" ./scripts/compile-all.sh
```

## Test Flow

1. Compile baseline:

   ```sh
   CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
   ```

2. Compile display path:

   ```sh
   CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
   ```

3. Serial smoke after upload:

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
   cal
   ```

4. Touch smoke with `USE_DISPLAY=1`: tap each game card, verify Pong drag,
   Snake swipes, 2048 swipes, `MENU`, and `RESTART`.

5. If touch is rotated or clipped, run `cal`, tap corners, read the raw and
   mapped coordinates from Serial, then rebuild with the calibration flags.

## Proof States

- `compile-ready`: both baseline and requested feature-flag builds compile.
- `uploaded`: the sketch was flashed to a named serial port.
- `touch-proven`: the real CrowPanel accepted touch in all three games.
- `sd-proven`: `USE_SD_HIGHSCORES=1` mounted SD_MMC and retained scores across
  reboot.

Do not claim `touch-proven`, `uploaded`, or `sd-proven` from a compile-only run.
