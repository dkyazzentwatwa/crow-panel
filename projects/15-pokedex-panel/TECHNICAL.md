# 15 - Pokedex Panel Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/15-pokedex-panel.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, SD-first catalog with visible mock fallback, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/15-pokedex-panel/TECHNICAL.md.
```

---

An offline Pokedex-style CrowPanel port of the local
`/Users/cypher/Documents/GitHub/esp32-pokedex` Cardputer app. The original app
uses a keyboard, small screen, audio, and MicroSD catalog; this port makes it a
large 1024x600 touch dashboard with Serial commands, a local U8g2 typography
stack, and an SD-first catalog path with a visible mock fallback.

## What it shows

- A browse/search result list on the left.
- A large detail card with generated Poke Ball art, type chips, GO stats, buddy
  distance, and second-move dust.
- Detail pages for trainer note, weaknesses, resistances, evolution, and moves.
- SD catalog streaming from the original `esp32-pokedex` layout by default.
- Mock catalog fallback with exact visible status text when SD mount or files fail.

The display is Arduino_GFX only, matching the rest of this repo. Sprite BMP
rendering and Cardputer audio are not ported yet.

## Serial Commands

| Command | Description |
|---|---|
| `status` | uptime, heap, flags, source mode, catalog state |
| `browse [start]` | show 8 catalog rows starting at `start` |
| `search <query>` | search name, dex number, type, or variant text |
| `rows` | print the current result/browse rows |
| `select <row>` | highlight a visible row |
| `open` | open the selected row |
| `open row <n>` | open visible row `n` |
| `open <query>` | search and open the first match |
| `page [next\|prev\|1-5]` | move through detail pages |
| `demo` | search `mega` and open the first dramatic card |
| `source` | show the source repo path and SD layout |
| `history` | recent events |
| `grid [ordinal]` | show a grid window |
| `letter <a-z>` | jump in A-Z order |
| `sort [dex\|name]` | set browse order |
| `filter [type\|none]` | set the type filter |
| `shadows [on\|off]` | toggle shadow forms |
| `sprite [entry_id]` | sprite decode report |

> Before 2026-07-31 the shared router's command table was capped at 12 entries
> and silently dropped the rest, so `letter`, `sort`, `filter`, `shadows` and
> `sprite` never dispatched. See `CROW_SERIAL_MAX_COMMANDS` in `AppConfig.h`.

Useful searches:

```text
search pikachu
search mega
search electric
open mewtwo
open row 2
```

## Touch Controls

- Tap a visible row to open it.
- Tap `LIST` to return from a detail page.
- Tap `PAGE -` / `PAGE +` or the right detail panel to move through detail pages.
- Tap `PREV` / `NEXT` on the list screen to page through browse rows.
- Tap `SEARCH` to open the full-screen touch keyboard. It supports QWERTY,
  shift, symbols, backspace, space, left/right cursor movement, Return, and
  cancel. Return submits the query; an empty query keeps the current results.

## SD Catalog Mode

Copy the contents of `/Users/cypher/Documents/GitHub/esp32-pokedex/sd/` to the
root of the card:

```text
/pokemon/index.csv
/pokemon/catalog_meta.json
/pokemon/data/*.json
/pokemon/sprites/*.bmp
/config/settings.json
/audio/...
```

The display-plus-SD build is:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1" ./scripts/compile-all.sh
```

The port mounts `SD_MMC`, verifies `/pokemon/index.csv`, reads that index and
`/pokemon/data/*.json`, and reports the full entry count in the header. It does
not read sprites or audio yet. Failure states remain visible, including
`SD MOUNT FAILED`, `MISSING /pokemon/index.csv`, `EMPTY /pokemon/index.csv`,
`SD DETAIL MISSING`, and invalid-detail states.

The exact upload command is:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1" ./scripts/upload-project.sh projects/15-pokedex-panel <bootloader-port>
```

**Bring-up order matters: mount SD before the display.** `setup()` calls
`catalog.begin()` (the `SD_MMC.begin()` mount) before `dashboard.begin()`
(`CrowDisplay::begin()`, DSI panel init). Doing it the other way around
produced a real black-screen hang on hardware: once the DSI panel had already
come up, the native SDMMC host's `SD_MMC.begin()` call never returned (found
via staged solid-color boot probes, since Serial is unusable once the panel
is running - see the 2026-07-27 session log). Every other SD+display project
in this repo (09 Cypher Tune MPC, 18 Cypher Desk, 22 Cypher Boy) mounts SD
before its display init for the same reason. Keep that order if this file is
ever restructured.

Project 15 uses the U8g2 library in display builds. The font stack is local to
the project and follows Project 18's compact `cubic11` approach for controls,
with larger bold faces for titles and names. Text width, centering, clipping,
and wrapping are measured through U8g2-compatible Arduino_GFX calls rather than
the shared `GFXfont` helpers.

## Proof State

- `compile-ready`: baseline, display, SD, and display+SD flag rows compile
  through the repo matrix.
- `uploaded`: **yes (2026-07-27).** The display+SD-pokedex build was flashed to
  a real panel (`/dev/cu.usbmodem1101`, hwcdc FQBN, hash verified) and the
  dashboard renders visibly, fixing a black-screen regression caused by
  mounting SD after the display instead of before it (see above).
- `field-proven`: not yet proven. Touch row/browse/detail navigation, the
  search keyboard, and full-card JSON browsing from a real `esp32-pokedex` SD
  card still need captured display/touch evidence on the actual panel.

## Source Boundary

This is a local educational field guide. It uses generated offline data from the
source project and does not connect to Pokemon GO, scrape accounts, or use game
services.
