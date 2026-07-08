# 15 - Pokedex Panel

An offline Pokedex-style CrowPanel port of the local
`/Users/cypher/Documents/GitHub/esp32-pokedex` Cardputer app. The original app
uses a keyboard, small screen, audio, and MicroSD catalog; this port makes it a
large 1024x600 touch dashboard with Serial commands and an SD-backed catalog path
behind a feature flag.

## What it shows

- A browse/search result list on the left.
- A large detail card with generated Poke Ball art, type chips, GO stats, buddy
  distance, and second-move dust.
- Detail pages for trainer note, weaknesses, resistances, evolution, and moves.
- Mock catalog entries by default, so the app is useful with no SD card.
- Optional SD catalog streaming from the original `esp32-pokedex` layout.

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

## SD Catalog Mode

Default builds use the built-in mock catalog. To try the full source catalog,
copy the contents of the `esp32-pokedex/sd/` folder to the root of the card:

```text
/pokemon/index.csv
/pokemon/catalog_meta.json
/pokemon/data/*.json
/pokemon/sprites/*.bmp
/config/settings.json
/audio/...
```

Then compile with:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1" ./scripts/compile-all.sh
```

The port reads `/pokemon/index.csv` and `/pokemon/data/*.json`. It does not read
sprites or audio yet.

## Proof State

- `compile-ready`: baseline, display, SD, and display+SD flag rows are intended
  to compile through the repo matrix.
- `uploaded`: not yet proven for this project.
- `field-proven`: not yet proven. SD_MMC mount, touch coordinates, and full-card
  JSON browsing still need captured Serial/display evidence on the actual panel.

## Source Boundary

This is a local educational field guide. It uses generated offline data from the
source project and does not connect to Pokemon GO, scrape accounts, or use game
services.
