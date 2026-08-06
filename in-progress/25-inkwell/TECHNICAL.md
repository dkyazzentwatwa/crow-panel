# Inkwell — Technical Notes

Under construction. This project is a scaffold only: `25-inkwell.ino` boots
the shared `SerialCommandRouter` + `EventLog` + `StatusReport` stack with no
hardware drivers wired in.

## Proof state

**compile-ready, scaffold only.** Verified with `arduino-cli compile` against
the esp32p4 FQBN used by the rest of the repo. Nothing has been uploaded to a
board and no hardware behavior has been observed.

## Config

`config/ProjectConfig.h` predeclares the flags and geometry later tasks will
use:

- `USE_INKWELL_SD` — real SD card access (off by default; mock-first)
- `INKWELL_ROTATION` — portrait rotation for `Arduino_DSI_Display`
  (1 = 90° CW, 3 = 90° CCW; default 1 until hardware bring-up decides)
- `INKWELL_PAGE_W` / `INKWELL_PAGE_H` — logical portrait page geometry
  (600 x 1024)

None of these flags are consumed yet; they exist so later tasks (parser, SD
loading, display) have an agreed-on place to read from.

## What's not here yet

- Text/EPUB parsing
- SD card book loading
- Display rendering (portrait DSI path)
- Touch input / page-turn UI
- Sample books

See the design and implementation plan docs for the full task sequence.
