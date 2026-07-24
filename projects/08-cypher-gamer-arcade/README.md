# Cypher Gamer Arcade

A touch arcade for the Elecrow CrowPanel Advanced 7-inch display.

Browse a catalog of games, tap a card to play, and pause any time for resume,
restart, scores, or quit. Pong has a draggable paddle, Snake is steered by
swipes, and 2048 merges tiles with swipes. A dedicated high-score screen tracks
your best run in each game, optionally saved to an SD card. Serial commands
drive the same game state as touch, so you can smoke-test everything without a
screen.

> This is Project 8 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready: the baseline, display, and both SD-high-score builds compile for
the real ESP32-P4 target. Touch play, the offscreen Pong playfield, and SD
high-score persistence have not been observed on a physical CrowPanel yet. See
the [technical reference](TECHNICAL.md) for the acceptance steps.

## What you get

- A **catalog** screen with large Widgets cards for each game, showing its best
  score, and a bottom tab bar that switches to the scores screen
- A per-game **high-score** screen; tap a card there to jump straight into that
  game
- A **pause overlay** reachable during any game with Resume / Restart / Scores /
  Quit buttons
- **Pong** — drag inside the playfield to move your paddle (the animated field
  is composited in an internal-SRAM offscreen canvas and blitted per frame)
- **Snake** — swipe anywhere to turn; reverse turns are ignored
- **2048** — swipe to slide and merge tiles
- Optional SD high-score persistence, with a clean fallback to RAM scores if the
  card does not mount
- A full Serial command set that exercises the same state touch uses, plus a
  headless `selftest`

## If touch feels rotated or clipped

Run the `touch` (or `cal`) command, tap each corner, read the raw and mapped
coordinates from Serial, then rebuild with the shared `CROW_TOUCH_*` calibration
flags set to match your panel. The details are in the
[technical reference](TECHNICAL.md).

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
