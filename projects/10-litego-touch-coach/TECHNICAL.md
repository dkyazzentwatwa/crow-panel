# CrowPanel LiteGo Touch Coach Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/10-litego-touch-coach.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/10-litego-touch-coach/TECHNICAL.md.
```

---

A playable 9x9 Go game. The rules engine and the opponent are plain C++ with no
Arduino dependency, so the exact translation units that ship in the firmware
also compile and run on the host — see [Host tests](#host-tests).

## Architecture

| File | Role |
| --- | --- |
| `src/GoBoard.{h,cpp}` | Rules core. Flat 81-point board, neighbour table, Zobrist hashing, positional superko, komi, eye detection, Tromp–Taylor scoring, snapshot save/restore. No `Arduino.h`, no `String`. |
| `src/GoAi.{h,cpp}` | Monte-Carlo opponent. Light playouts with an eye guard and a local-atari policy, UCB1 allocation over root moves seeded with a one-ply prior, cooperative time slicing. Also pure C++. |
| `src/GoFixtures.{h,cpp}` | The 28 rules fixtures, shared by the device `selftest` and the host harness so both run identical checks. |
| `src/LiteGoGame.{h,cpp}` | Arduino session layer. Undo stack, AI driver, komi/level/colour settings, and every `String` the UI and Serial need. |
| `src/LiteGoTouchView.{h,cpp}` | Touch mapping and the dirty-region renderer. |
| `test/host_main.cpp` | Host entry point. Outside `src/` on purpose — arduino-cli only compiles the sketch root and `src/`, so it never reaches the firmware. |

### Why the search is sliced, not threaded

`GoAi::start()` snapshots the root position and `GoAi::step(sliceMs)` is called
from `loop()` until it returns true. That keeps touch responsive and the
thinking bar moving without a second task or any locking, and it makes the
search reproducible — the host harness installs no clock, so a host run executes
exactly `maxPlayouts` playouts and is byte-for-byte repeatable.

### Rendering

The panel is opened with `CrowDisplay::begin(..., /*manualFlush=*/true)`.
With the default `auto_flush=true`, Arduino_GFX calls `esp_cache_msync` once per
drawn primitive — per glyph pixel for text — so a full-screen repaint costs
thousands of cache syncs. With manual flush, drawing only touches the cached
framebuffer and each render pass issues **one** sync covering the union of the
rows that changed.

Nothing calls `fillScreen` after boot. Placing a stone redraws the affected
cells only: the new stone, any captured points, and the cell that used to carry
the last-move marker. That is also why the single-framebuffer DSI panel never
flashes mid-update.

## Controls

- Serial is a complete play surface in every build, display or not.
- `USE_DISPLAY=1` adds the touch board.
- **Placement is preview-then-confirm**: the first tap parks a ghost stone on
  the nearest intersection, the second tap on the same point commits it. Cells
  are 48 px and a fingertip is wider than that, so committing on first contact
  misplaces stones no matter how good the calibration is.
- Buttons act on **release inside the target**, so sliding a finger off cancels.
- Board taps are ignored while the opponent is thinking — otherwise a tap would
  place a stone of the opponent's colour, since the engine always plays for the
  side to move.

## Serial commands

| Command | Effect |
| --- | --- |
| `help` / `status` / `history` | router help, game settings, event log |
| `board` | print the 9x9 board |
| `play <x> <y>` | play a point, coordinates 0-8 |
| `pass` / `undo` / `resign` | pass, take back your move and the reply, resign |
| `new` / `reset` | start a new game |
| `cpu` | make one opponent move now (blocking) |
| `score` | area score with komi |
| `hint` | repeat the coach line |
| `level <easy\|normal\|hard>` | set opponent strength |
| `komi <points>` | set komi, e.g. `komi 6.5` |
| `color <b\|w>` | choose the side you play |
| `selftest` | 28 rules fixtures plus AI hygiene checks |
| `bench <ms>` | measure playouts/sec on this board |
| `touchcal` | print raw and mapped touch coordinates |
| `autoplay <n>` | let the opponent play n moves |

Coordinates are zero-based: `play 0 0` is the upper-left point, `play 8 8` the
lower-right.

## Rule coverage

- Group liberty tracking, captures at zero liberties.
- Suicide prevention, **including the exception**: a move whose own group would
  have no liberty is legal when it captures first and thereby gains one.
- **Positional superko** via Zobrist hashing over the position history, which
  subsumes simple ko and stops a triple ko from looping forever.
- Komi, carried doubled (`LITEGO_KOMI_X2`, default 13 = 6.5) so scoring stays
  integer arithmetic and no game can be a draw.
- Two consecutive passes or a resignation end the game; the board is frozen
  afterwards and further moves are rejected.
- Undo via 200 position snapshots (~20 KB static), which restores captures, the
  side to move, and the ko history exactly.
- True-eye detection: all orthogonal neighbours own or off-board, plus ≥3 of 4
  friendly diagonals for an interior point and all 4 for an edge or corner
  (off-board diagonals count as friendly).

### Scoring

Tromp–Taylor area scoring — stones plus enclosed empty regions, komi applied,
neutral regions reported separately. There is **no dead-stone marking and no
seki adjudication**. This is why the opponent plays every dame out rather than
passing early: filling neutral points is score-neutral under area scoring, and
playing to the end is what guarantees no dead stones are left to be mis-counted.

## Opponent

| Level | Behaviour |
| --- | --- |
| `easy` | One-ply heuristic — captures, atari pressure, liberties, centre pull, random tiebreak. Instant, and with the eye guard it no longer kills its own groups. |
| `normal` | Monte-Carlo playouts, `budgetMs` 2000, cap 8000 playouts. |
| `hard` | Monte-Carlo playouts, `budgetMs` 4000, cap 40000 playouts. |

The first on-panel game showed `normal` playing near heuristic strength: at the
original 1.2 s budget the P4 (far slower than the host) ran too few playouts to
overcome the priors. The budgets above are the raised values; after each move
the opponent prints its achieved playout count to Serial **and to the on-screen
status line**, so the real device depth is visible without a live serial port.
Retune `budgetMs` from that number.

Search details:

- Root candidates are every legal point that is not one of the mover's own true
  eyes. Passing is deliberately **not** a candidate; with a small budget its win
  rate is noise, and an early pass would leave dead stones for the scorer.
  `shouldAcceptEnd()` handles the one case where passing is right: the opponent
  just passed and the current area score says we are ahead.
- Each candidate is seeded with 8 virtual playouts weighted by its one-ply
  score, mapped onto a 35–65% band. On a 400 MHz part the whole budget buys only
  a handful of playouts per candidate, which alone is close to random; the prior
  is what makes a short search still play sensibly, and real playouts overturn
  it as the budget grows.
- Playouts are uniform random over legal non-eye points, with a configurable
  chance (`atariPercent`) of answering a local atari — capturing the stone that
  just moved if it left itself on one liberty, otherwise running a neighbouring
  friendly group out of atari.
- Playouts narrow the superko window to 2 plies. Simple-ko correctness is all a
  playout needs, and the full history scan would otherwise sit on the hot path.

### Tuning the budgets

`budgetMs` values are what bind on device; `maxPlayouts` is only a ceiling.
**Measure before trusting them**: run `bench 1000` on the panel, then set the
level budgets from that number. Host playout rates are not a guide — the host is
roughly an order of magnitude faster.

```text
bench 1000
```

## Touch calibration

The panel reports touch in screen coordinates already — no axis swap, no
inversion, 0..1023 by 0..599. These defaults match what projects 08, 18, and 21
use on the same hardware. Override in `config/ProjectConfig.h` or with `-D`
flags if a board revision ever needs different values:

```c
LITEGO_TOUCH_SWAP_XY   0
LITEGO_TOUCH_INVERT_X  0
LITEGO_TOUCH_INVERT_Y  0
LITEGO_TOUCH_MIN_X 0   LITEGO_TOUCH_MAX_X 1023
LITEGO_TOUCH_MIN_Y 0   LITEGO_TOUCH_MAX_Y 599
```

Confirm on hardware by holding a finger on the screen and running `touchcal`,
which prints the raw and mapped coordinates side by side.

## Other configuration

| Define | Default | Meaning |
| --- | --- | --- |
| `LITEGO_KOMI_X2` | `13` | Komi doubled; 13 is 6.5 points |
| `LITEGO_DEFAULT_LEVEL` | `1` | 0 easy, 1 normal, 2 hard |
| `LITEGO_HUMAN_COLOR` | `'B'` | The side you play |
| `LITEGO_AI_SLICE_MS` | `40` | Search milliseconds per `loop()` pass |

## Host tests

```sh
./scripts/test-litego.sh           # fixtures, AI hygiene, bench, strength tournament
./scripts/test-litego.sh --quick   # skip the tournament
```

Builds `GoBoard.cpp`, `GoAi.cpp`, `GoFixtures.cpp`, and `test/host_main.cpp`
with `g++ -std=c++17 -O2 -Wall -Wextra -Werror`. It runs the 28 rules fixtures,
then asserts across self-play games that every level produces only legal moves,
never fills its own eye, and terminates; prints the playout rate; and plays a
10-game tournament with alternating colours requiring hard to beat easy at least
7 times.

Last host run: 28/28 fixtures, all hygiene checks green, hard beat easy 8/10 at
1200 playouts. The harness also prints a host playout rate, but it swings by
roughly 3x with machine load (2 000–8 500/sec observed on the same Mac) and is
not a substitute for the on-device `bench` number.

## Build and upload

Baseline:

```sh
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}"
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries ./shared \
  --build-path ./_arduino-build/10-litego-baseline \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/10-litego-touch-coach
```

Display:

```sh
CTAGS_WORKAROUND=1 arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries ./shared \
  --build-path ./_arduino-build/10-litego-display \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/10-litego-touch-coach
```

Upload when a CrowPanel serial port is present:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" \
  ./scripts/upload-project.sh projects/10-litego-touch-coach /dev/cu.usbmodemXXXX
```

## Proof state

- `compile-ready`: Arduino CLI build succeeds for baseline and/or display flags.
- `uploaded`: the sketch has been flashed to a detected CrowPanel serial port.
- `field-proven`: the physical board has shown the UI, accepted touch moves, and
  produced matching Serial proof.

Current proof state: **uploaded**. The `USE_DISPLAY=1` build was flashed to a
CrowPanel on `/dev/cu.usbmodem1101` — `esptool` wrote 282066 compressed bytes
and reported "Hash of data verified", then hard-reset the board. Baseline and
display both compile on the ESP32-P4 FQBN, and the rules engine and AI are
**host-tested** (28/28 fixtures and all hygiene checks green).

Not yet claimed: `field-proven`. The flash and its hash verification prove the
binary is on the board; they prove nothing about what it does once running. The
panel's native USB-CDC serial drops within seconds of boot (the port
disappeared right after this upload, as expected), so `field-proven` needs
someone watching the screen for:

1. `selftest` reports overall PASS.
2. `bench 1000` records a real playout rate, and the level budgets are retuned
   from it.
3. `touchcal` confirms raw and mapped coordinates agree while a finger is down.
4. Preview-then-confirm places a stone on the intended intersection every time.
5. The opponent replies within its budget with a live thinking bar, and redraw
   shows no full-screen flash.
6. A game is played to two passes, producing a declared result with komi, and
   undo, resign, and NEW GAME all behave.

Note from earlier sessions on this hardware: native USB-CDC serial drops within
seconds of the app starting, so most of step 1-6 is a visual check on the panel.
The `selftest` verdict, `bench` rate, and `touchcal` readout are therefore also
surfaced in the on-screen status panel, not only on Serial.
