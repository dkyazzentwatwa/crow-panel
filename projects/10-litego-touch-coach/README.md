# LiteGo Touch Coach

A playable 9x9 Go game for the Elecrow CrowPanel Advanced 7-inch touchscreen.

Sit down with the panel and play a real game against a Monte-Carlo opponent —
tap an intersection to line up your stone, tap it again to place it, and the
opponent thinks out loud on a progress bar while it searches. Games end with a
declared winner and margin, komi included. It runs entirely offline: no server,
no PWA, no network.

> This is Project 10 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready for the baseline and `USE_DISPLAY=1` builds on the ESP32-P4
target, and the rules engine and AI pass their full test suite on the host. The
board has not been played on a physical CrowPanel yet — the screen, the taps,
the opponent's on-device speed, and the `selftest` all still need to be observed
there. See the [technical reference](TECHNICAL.md).

## Playing

- **Place a stone** — tap an intersection to preview a ghost stone, then tap the
  same point to commit. A fingertip is wider than a grid cell, so the second tap
  is what makes placement accurate.
- **Buttons** — `PASS`, `UNDO`, `HINT`, `SCORE`, `RESIGN` on the top row;
  `NEW GAME`, `LEVEL`, and `SWAP SIDES` on the bottom.
- **Difficulty** — `LEVEL` cycles easy → normal → hard. Easy answers instantly
  from a one-ply heuristic; normal and hard run Monte-Carlo playouts within a
  time budget.
- **Everything also works over Serial**, and every touch action goes through the
  same code path as its command.

## What you get

- A full 9x9 board with last-move marker, capture animation-free instant redraw,
  and a game-over overlay showing the result
- A Monte-Carlo opponent that plays a different game every time, never fills its
  own eyes, and passes when passing is right
- Real scoring: Tromp–Taylor area scoring with komi (6.5 by default), so games
  have a winner and a margin, never a tie
- Full rules: captures, suicide prevention, the capturing-suicide exception, and
  **positional superko** — triple kos cannot loop
- Undo that takes back your move and the opponent's reply together
- Liberty and atari coaching after every move
- A `selftest` that runs 28 rules fixtures plus AI hygiene checks on the device,
  and a `bench` that reports the opponent's real playout rate

## A note on scoring

Scoring is Tromp–Taylor area scoring with no dead-stone marking, which is why
the opponent plays every neutral point out instead of passing early. Filling
dame costs nothing under area scoring, and playing to the end is what keeps the
final number honest — there are no dead stones left on the board to mis-count.
There is no seki adjudication.

## Technical reference

For build flags, touch calibration, difficulty tuning, upload commands, the host
test harness, file layout, and proof terminology, see [TECHNICAL.md](TECHNICAL.md).
