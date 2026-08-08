# Inkwell Reader — technical reference

Proof state: **compile-ready**. Four flag combos build green for
`esp32:esp32:esp32p4` (core 3.3.8): baseline 447 KB, `USE_INKWELL_SD`
517 KB, `USE_DISPLAY` 577 KB, kitchen-sink 700 KB — with real library
linkage verified under `<build-path>/libraries/` (GFX/SensorLib/SD_MMC/
JPEGDEC/PNGdec present exactly when their flags are on). **Nothing has
run on a real CrowPanel.** The repo-wide `compile-all` / flag-matrix
sweeps were deliberately skipped in this round (user call); run them
before merging to main.

## Architecture

```
TxtParser ─┐
MarkdownParser ─┼─> InkDoc blocks ─> Paginator (TextMeasure callback) ─> pages
XhtmlParser ─┘         ^                    ^
EpubBook (vendored miniz) ─ InkBook facade ─┘
```

- The whole core above is Arduino-free and host-tested:
  `./scripts/test-inkwell.sh` — 545 checks covering parser fixtures,
  malformed EPUBs, pagination arithmetic (hand-counted in test comments),
  resume offsets, perf scaling guards (linear-vs-quadratic ratio
  assertions with min-of-5 timing), and the touch gesture state machine
  (scripted contact streams on a fake clock).
- One pipeline on device: serial commands and touch gestures call the same
  action cores (`nextPage`/`gotoPermille`/…); every state change funnels
  through `renderCurrent()` (Serial print + panel draw).
- Touch: `src/InkGestures.h` (pure, host-tested) turns the GT911 contact
  stream into debounced one-shot events — CrowTouch's 30 ms release
  window (a dropout mid-swipe must not split the gesture), taps on
  release at the press point, swipes at ≥80 px of ≥1.5:1 horizontal
  travel, wandering drags fire nothing. The shared `CrowTouch` itself is
  landscape-bound (clamps Y at 599), hence the project-local recognizer,
  same as projects 09/21/22. Swipes page the reader, library grid, and
  TOC; a press on the HUD scrub bar becomes a live drag (throttled ~15 Hz
  repaints, jump commits on release).
- Flash cadence: the e-ink invert flash fires on open/chapter/jump
  (`requestFullFlash()` in the action cores) and every 6th sequential
  turn — never on plain rerenders (HUD close, Aa tweaks). Flashing every
  render read as constant flicker on glass (2026-08-07).
- Layout metrics: `SerialMeasure` (host-parity tables) with all flags
  off; `GfxMeasure` (glyph-advance cache over the vendored FreeSerif/
  FreeMono GFXfonts, int16-saturating) when the display is up.

## Format subsets (degrade, never crash)

- **Markdown**: `#`–`###` headings (deeper → H3), bold/italic/mono,
  lists (tab or 2-space nesting, depth ≤ 3), quotes, fenced code
  (verbatim), rules, links keep text, images drop. No tables/setext/
  entities — kept literal (see `MarkdownParser.h`'s degrade list).
- **XHTML (EPUB chapters)**: h1–h6/p/div/li/pre/blockquote/hr/br +
  inline styles as nesting counters; script/style consumed as raw text;
  comments/DOCTYPE skipped; quote-aware attribute scanning; entities
  incl. numeric (saturating, NUL dropped); everything else transparent.
- **EPUB**: container → OPF (namespace-tolerant, attributed metadata
  tags OK) → spine (xhtml-only, first-wins duplicates) → TOC (nav doc
  or NCX, nested entries flattened, unresolved hrefs kept with
  `spineIndex -1`) → cover (properties or meta). `open()` fails only on
  unreadable zip / missing container / missing OPF. 16 MB per-entry cap
  (`kMaxEntryBytes`); oversized chapters read as size 0 and unreadable.

## Memory model (32 MB PSRAM, ~300 KB internal DRAM free)

- Whole `.epub`/book file in one PSRAM buffer (`heap_caps_malloc`,
  `MALLOC_CAP_SPIRAM`), single slot in `LibraryStore`; 12 MB/book cap so
  two books always fit during the old+new swap overlap. Freed on close.
- Chapters parse on demand; blocks + laid-out lines live in normal heap —
  each `Line` costs ~150 B + ~3 small allocations in INTERNAL DRAM (the
  small-alloc path never goes to PSRAM), so very long chapters are the
  known memory risk to watch at bring-up.
- `permille` progress is uncompressed-byte-weighted (chapter sizes from
  zip stat, no decompression).

## SD formats (all Arduino FS via SD_MMC — never C stdio)

- `/books/.inkwell/catalog.txt` — `name|size|title|author` per line
  ('|' in titles → '/'); cache key is name+size (no mtime: an in-place
  re-export at identical byte count keeps stale metadata until renamed).
- `<book>.pos` — `spine=N off=N pct=N`, tolerant parse, garbled → 0.
- `<book>.<chapter>.<layouthash>.idx` — one page-start offset per line;
  write-only in v1 (stale-hash cleanup on write; nothing reads them yet).
- `<book>.thumb` — `uint16 w,h` + 220×300 RGB565; written the first time
  an EPUB is opened (while its bytes are resident — grid-time decode
  would evict the single-slot book buffer), read by the library grid.
- exFAT cards are detected (`totalBytes()==0`) and refused with a
  "reformat as FAT32" log, matching project 18.

## Fonts

Vendored Adafruit GFX FreeFonts (BSD) in `src/fonts/`, `#include
<Adafruit_GFX.h>` line stripped per `shared/CrowPanelShared/fonts/`
convention, included from exactly one TU (`GfxMeasure.cpp`). ASCII
0x20–0x7E only — non-ASCII draws blank and the paginator's hard-split is
byte-based, so multibyte text may split mid-codepoint (accepted v1
limit). Font table: body Serif 9/12/18 pt by step; headings bold one
size up capped at 24 pt; mono steps with body.

## Hardware bring-up (in order, one flag at a time)

1. `USE_DISPLAY=1` only. **First check: rotation.** The shared
   `CrowDisplay` rotation (new in this branch) is derived from
   Arduino_DSI_Display's address math but NOT hardware-verified: confirm
   text reads top-to-bottom in portrait and tap all four corners; if
   mirrored, flip `INKWELL_ROTATION` 1↔3 in `config/ProjectConfig.h`.
   Then: page draw correctness (baseline placement is a 3/4-line-box
   approximation), flash timing feel, tap zones.
2. `+USE_INKWELL_SD` with a FAT32 card holding a few real books incl. a
   large EPUB: scan time, catalog reuse on reboot, position save/load,
   sidecar writes.
3. Covers: open an EPUB with a JPEG cover, then one with PNG; reboot and
   confirm the grid blits the cache.

Known-unverified risk list: rotation quadrant mapping; GT911 remap
agreement with the panel; GFX baseline placement; JPEGDEC/PNGdec decode
paths (never executed); SD single-owner interaction if another subsystem
ever mounts; long-chapter DRAM pressure.
