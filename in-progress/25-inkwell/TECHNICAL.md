# Inkwell — Technical Notes

Under construction. `25-inkwell.ino` is a serial mock reader: the shared
`SerialCommandRouter` + `EventLog` + `StatusReport` stack, plus the full
text/EPUB core (`src/TxtParser`, `MarkdownParser`, `XhtmlParser`, `EpubBook`,
`Paginator`, `InkBook`) and a mock `LibraryStore` serving three embedded
samples (`src/SampleBooks.h`). No hardware drivers (display, touch, SD) are
wired in yet.

## Proof state

**compile-ready, serial mock reader.** Verified with `arduino-cli compile`
against the esp32p4 FQBN used by the rest of the repo; the host test suite
(`scripts/test-inkwell.sh`) is at 501 checks covering the parsers, EPUB
container walk, paginator, and the two embedded samples. Nothing has been
uploaded to a board and no hardware behavior has been observed — pagination
and page turns are only proven against `SerialMeasure`'s fixed per-style
metrics, not real glyph widths.

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

- SD card book loading (`LibraryStore` is an in-RAM mock of 3 embedded
  samples; positions don't survive a reboot)
- Display rendering (portrait DSI path) — pages render as text to Serial
- Touch input / page-turn UI

See the design and implementation plan docs for the full task sequence.
