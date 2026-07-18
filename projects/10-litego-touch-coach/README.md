# CrowPanel LiteGo Touch Coach

Arduino C++ 9x9 Go coach inspired by the `ai-go` PWA. The rules engine lives in
project-local `src/` code so the sketch stays a small Serial and touch harness.

## Controls

- Serial is the smoke-test path in every build.
- `USE_DISPLAY=1` adds a full-screen 9x9 touch coach surface inspired by the
  ADS-B radar dashboard style: dense header/footer, status cards, command pills,
  last-move highlighting, wrapped coach text, and explicit proof-state footer.
- Touch input tries raw, swapped, and flipped GT911 coordinate mappings before
  resolving a tap. Every touch action routes through the same game path as the
  matching Serial command.
- Touch command pills: `PASS`, `CPU`, `HINT`, `SCORE`, and `RESET`.

## Serial Commands

- `help` / `status` / `history`
- `board`
- `hint`
- `play <x> <y>`
- `cpu`
- `pass`
- `reset`
- `score`
- `selftest`

Coordinates are zero-based: `play 0 0` is the upper-left point and `play 8 8`
is the lower-right point.

## Rule Coverage

- Empty-point legal move checks.
- Group liberty tracking across connected stones.
- Captures when neighboring opponent groups reach zero liberties.
- Suicide prevention unless the move captures and creates liberties.
- Simple ko prevention against immediate board-position recapture.
- Pass and reset.
- Simple CPU move selection that prefers captures, atari pressure, liberties,
  and central points.
- Liberty and atari coaching after each move.
- Built-in `selftest` smoke runner for capture, suicide rejection, CPU move,
  pass/end, reset, score, and a simple ko fixture.
- Rough area scoring: stones plus enclosed empty regions, with neutral regions
  split out. No komi or seki adjudication is applied.

## Smoke Scenarios

Capture one white stone:

```text
reset
play 1 0
play 0 0
play 0 1
play 2 2
play 1 1
```

Suicide rejection:

```text
reset
play 0 1
play 4 4
play 1 0
play 5 5
play 1 1
play 0 0
```

Pass/reset/scoring:

```text
reset
play 4 4
cpu
score
pass
pass
reset
```

Rules self-test:

```text
selftest
```

Display compile row:

```sh
FQBN="${FQBN:-esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600}"
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries ./shared \
  --build-path ./_arduino-build/10-litego-touch-coach-display \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/10-litego-touch-coach
```

Upload when a CrowPanel serial port is present:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" \
  ./scripts/upload-project.sh projects/10-litego-touch-coach /dev/cu.usbmodemXXXX
```

## Proof State

- `compile-ready`: Arduino CLI build succeeds for baseline and/or display flags.
- `uploaded`: the sketch has been flashed to a detected CrowPanel serial port.
- `field-proven`: the physical board has shown the UI, accepted touch moves,
  and produced matching Serial proof.

Current proof state: compile-ready for baseline and `USE_DISPLAY=1` builds on
the ESP32-P4 FQBN. Do not claim uploaded or field-proven until a detected
CrowPanel port accepts the `USE_DISPLAY=1` upload, the screen is observed, the
board intersections and command pills respond to touch, and Serial `selftest`
passes on the running device.
