# Project 15 Pokedex Panel — expo pass design

Date: 2026-07-29
Project: `projects/15-pokedex-panel`
Status: design approved, not implemented

## Problem

Project 15 works but reads as a developer demo, not a Pokedex. Three things cause that:

1. **No artwork.** Every one of the 1573 entries renders the same generic vector
   pokeball. The SD card already carries 1575 sprites the firmware never opens.
2. **Unusable browse.** `POKEDEX_MAX_RESULTS` is 8 rows per page against 1573
   entries — 197 pages, reachable only by tapping PREV/NEXT one page at a time.
   There is no jump-to-top, no letter jump, and no filter.
3. **Developer-facing screen.** A third of the browse screen is a `PORT NOTES`
   panel describing Cardputer porting history, and the hero panel instructs
   visitors to type `search pikachu` into a serial console they do not have.

Underneath all three sits a performance defect: `PokedexCatalog::browse(start, …)`
reopens `/pokemon/index.csv` and linearly `readStringUntil('\n')`s from line 0 on
every page turn (`src/PokedexCatalog.cpp:272`). Reaching page 100 costs ~1000
String allocations off SD. Letter-jump, filtering and sorting cannot be built on
top of that access pattern.

## Context: what is already on the SD card

Source payload lives at `~/Documents/GitHub/esp32-pokedex/sd/pokemon/` (19 MB).

| Path | Contents | Used by project 15 today |
|---|---|---|
| `index.csv` | 1573 rows, `dex,entry_id,name,type1,type2,file` | yes |
| `data/*.json` | per-entry detail, includes a `"sprite"` path field | yes, except `sprite` |
| `sprites/*.bmp` | **1575 sprites**, 40x40 24-bit, ~4.8 KB each, 13 MB total | **no** |
| `audio/bgm/pokedex_loop.wav` | chiptune loop, 11025 Hz mono unsigned 8-bit | **no** |
| `config/settings.json` | Cardputer-era settings | no |

Sprite BMPs are Windows 3.x, 54-byte header, bottom-up rows, 24 bpp. At 40 px
wide a row is 120 bytes, already a multiple of 4, so there is **no row padding
to handle**.

`index.csv` type1 values are exactly 18: Bug, Dark, Dragon, Electric, Fairy,
Fighting, Fire, Flying, Ghost, Grass, Ground, Ice, Normal, Poison, Psychic,
Rock, Steel, Water.

Variant breakdown of the 1573 entries, measured by running the shipped
`classifyVariant` over the real file (`marker appears anywhere in entry_id`, so
these overlap):

| Marker | Count |
|---|---|
| base (no underscore) | 907 |
| shadow | 459 |
| regional (galarian / alolan / hisuian) | 71 |
| other (costume and unrecognised forms) | 107 |
| mega | 48 |
| **carrying 2+ marker bits** | **19** |

Browse with shadows hidden therefore shows **1114** of 1573.

Counting by the *last* underscore token instead gives 46 megas and 50 regionals —
both wrong, because `charizard_mega_x` ends in `x` and `rattata_alolan_shadow`
ends in `shadow`. Do not re-derive these numbers that way. Likewise, 43 entries
have two or more underscores but only 19 carry two or more marker *bits*
(`pikachu_5th_anniversary` has two underscores and one marker).

## Decisions

Settled during brainstorming:

- **Operator-driven, not a kiosk.** The owner holds the panel and narrates to
  visitors. Optimise for reaching an interesting entry fast. **No attract/idle
  mode** — explicitly out of scope (YAGNI).
- **Hide shadow forms from browse by default**, with a footer toggle to show
  them. Megas and regionals stay inline as their own rows — they are visually
  distinct and worth showing. Search still reaches shadows regardless of the
  toggle. Default browse count drops 1573 -> 1114.
- **Browse is a sprite grid**, 6 columns x 3 rows = 18 tiles per screen, not a row
  list. Sprites are the interface. At the default 1114 entries that is ~62
  screens, though the window is offset-based rather than page-aligned (see
  "Browse is offset-based" below).
- **One spec, four stages**, executed index -> sprites -> UI -> audio so each
  stage is verifiable on the board before the next begins.

## Architecture

Three new units join the existing `PokedexCatalog`. Each has one job and can be
understood and tested without reading the others.

| Unit | Job | Depends on |
|---|---|---|
| `PokedexIndex` | Build a compact in-PSRAM row index once at boot; answer paging, letter-jump, filter and sort queries from RAM | `index.csv` |
| `PokedexSprites` | Decode a sprite BMP to RGB565, nearest-neighbour upscale, LRU cache | SD, `PokedexIndex` |
| `PokedexAudio` | BGM loop plus SFX over I2S | SD |

`PokedexCatalog` keeps its current public API and its mock fallback. It gains an
internal delegation to `PokedexIndex` when SD is live. The mock path stays fully
intact so the project still boots and demos with every flag off, per the repo's
mock-first rule.

### Host-testability constraint

`PokedexIndex` and the BMP decoder must be **free of `Arduino.h`, `String`, and
`SD_MMC`** — `stdint.h` only — so the identical translation units that ship in
the firmware also compile under plain `g++`. This follows the established pattern
in `projects/10-litego-touch-coach/src/GoBoard.h` and its harness at
`projects/10-litego-touch-coach/test/host_main.cpp`, driven by
`scripts/test-litego.sh`. `arduino-cli` compiles only the sketch root and `src/`,
so a `test/` directory never reaches the firmware.

SD access is injected through a pure-virtual `PokedexByteSource` (`seek`, `read`,
`size`). Firmware implements it over `File`; host tests implement it over an
in-memory buffer. Buffer allocation is **caller-provided** rather than internal,
which makes the PSRAM-failure fallback explicit at the call site and lets host
tests use plain `malloc`.

### PokedexIndex

One pass over `index.csv` at boot builds a fixed 40-byte record per row:

| Field | Type | Purpose |
|---|---|---|
| `offset` | `uint32` | byte offset of the row in `index.csv`, for direct `seek()` |
| `dex` | `uint16` | national dex number (max observed 1019) |
| `flags` | `uint8` | variant bitmask: base / shadow / mega / regional / other |
| `type1` | `uint8` | index into the 18-type table |
| `name` | `char[32]` | display name, for sort and letter-jump without touching SD |

`name` is 32 bytes because the longest display name in `index.csv` is 28
characters (`Thundurus (Incarnate) Shadow`); anything shorter silently truncates
and corrupts A-Z ordering at the tail.

1573 x 40 B ~= 63 KB, allocated in PSRAM. After boot, `browse`, letter-jump and
filtering are pure RAM arithmetic; loading one row is a single `seek()` plus one
line read instead of up to 1573 sequential reads.

`flags` is a genuine bitmask, not an enum, because **variants compose**:
`rattata_alolan_shadow` is regional *and* shadow, and `charizard_mega_x` /
`charizard_mega_y` are distinct mega forms. 19 entries carry two or more variant
markers. The classifier must test every marker against the `entry_id` rather than
stopping at the first match — a first-match-wins implementation misclassifies all
19 and silently loses 2 megas. Costume forms (`pikachu_pop_star`,
`pikachu_5th_anniversary`) classify as `other`.

The bitmask is also what makes the shadow toggle a bit test rather than a string
compare on every row.

`index.csv` has no quoted fields, no embedded commas, and exactly 6 fields per
row, so the parser is a plain comma splitter — no CSV quoting rules needed.

Plus one order array: `uint16` row indices sorted by name, 1573 x 2 B = ~3 KB.
`index.csv` is stored in dex order, so an alphabetical A-Z rail needs this second
ordering to mean anything. Both orders are therefore first-class (see
"Browse order" below).

Queries the index must answer:

- `pageAtOrdinal(startOrdinal, order, filter)` -> up to 18 row handles beginning
  exactly at `startOrdinal` in the filtered, ordered sequence
- `ordinalOfLetter(char, filter)` -> ordinal of the first name >= letter, name
  order only
- `ordinalOfDex(value, filter)` -> ordinal of the first dex >= value, dex order
  only
- `countMatching(filter)` -> total, for the range readout and clamping
- `resolve(handle)` -> full `PokedexRow` via `seek()`

`order` is `kOrderDex` or `kOrderName`. `filter` carries the shadow toggle state
and the optional type selection. Filtering is independent of ordering — every
combination of order and filter must page correctly, including a filter that
matches zero rows.

### Browse is offset-based, not page-based

**Decided after measuring the alternative on real data.** Browse tracks a row
*offset* into the filtered sequence, not a page number.

A fixed 18-row page window can only ever land a jump on the page *containing* the
target. Measured against the real catalog: tapping `B` returns the correct page,
but the first B (`Bagon`) sits at **slot 14 of 18** behind fourteen A-names, and
10 of the 11 dex buckets land mid-page the same way. For a jump rail whose entire
purpose is reaching an entry fast, that is a failure.

With an offset, tapping `B` sets the offset to `ordinalOfLetter('B')` and `Bagon`
renders at slot 0. `PREV`/`NEXT` step the offset by 18 and clamp to
`[0, countMatching-1]`. Because the window is no longer page-aligned, the footer
shows a **range** (`51-68 of 1114`) rather than `Page 3/62` — which is more
informative anyway, and is what the header already wanted for the filter count.

Consequence for tests: an 18-row-or-smaller fixture cannot detect landing errors,
because everything fits in the first window. Index fixtures must exercise more
than one window's worth of rows.

### PokedexSprites

Decode path: read 4854-byte BMP -> validate header -> convert 24 bpp bottom-up
to RGB565 top-down -> integer nearest-neighbour upscale.

- 2x -> 80x80 grid tile
- 8x -> 320x320 hero image (fits the existing 360 px hero panel width)

PSRAM cache budget: 18 tiles x 80x80x2 B = ~230 KB, plus one 320x320x2 B = 205 KB
hero buffer. LRU eviction. Grid path constructs `/pokemon/sprites/<entry_id>.bmp`
from the index; the detail path prefers the `"sprite"` field already present in
the entry's JSON.

A page turn decodes up to 18 sprites (~86 KB of SD reads). Repaint time is to be
**measured on hardware and reported**, not predicted here.

Missing or malformed sprite falls back to the existing generic pokeball, so an
incomplete card degrades visibly rather than crashing or rendering blank.

### PokedexAudio

Modelled directly on `projects/20-pipboy-terminal/src/PipBoyMedia.cpp`, which is
hardware-verified for SD WAV streaming on this panel. Reuse its amp sequence
exactly: park amp off -> open stream -> push a block of silence -> raise enable.

`IO30 is ACTIVE-LOW.` Write it as
`digitalWrite(profile.audio.control, controlActiveHigh ? HIGH : LOW)` and read
`controlActiveHigh` from `HardwareProfile`. Driving it HIGH mutes the speaker
while I2S keeps streaming, which presents as "everything works and is silent".

`pokedex_loop.wav` is 11025 Hz mono unsigned 8-bit and must be converted to
16-bit 16 kHz mono before use — run it through the `convert-crowpanel-audio`
skill and place the result on the card. SFX (select, back, page, error, search)
generate as I2S tones in firmware, matching what the Cardputer original did with
`M5Cardputer.Speaker`.

Audio credits: `audio/credits.txt` on the card states the loop is a
project-local original chiptune, safe to redistribute. Do not add ripped OST.

## UI

### Browse order

Two orders, toggled by the footer `SORT` button:

| Order | Rail | Default |
|---|---|---|
| `DEX` — national dex number, the order `index.csv` is already in | dex hundreds: `0, 100, 200 … 1000` | yes |
| `A-Z` — alphabetical by display name | letters `A`-`Z` | no |

Dex order is the default because it is what a Pokedex is expected to do, and it
needs no sorted array to page. A-Z order exists because it is the only way a
letter rail means anything, and it is how you find a Pokemon a visitor names out
loud. The rail's contents swap with the order; there is never an A-Z rail over a
dex-ordered list.

### Browse screen (1024x600)

Replaces the current three-panel list screen.

- **Header** (unchanged height, 84 px): pokeball glyph, `POKEDEX`, and a
  subtitle showing active filter plus count (`1114 entries - shadows hidden`, or
  `Grass - 91 of 1114`). SD/mock state badge on the right. The existing header
  truncates its status to `SD catalog // 1573 e...`; the subtitle must fit or
  ellipsise deliberately.
- **Jump rail**, ~40 px wide on the left. Contents follow the active browse
  order: letters `A`-`Z` in name order, or dex hundreds `0, 100, 200 … 1000` in
  dex order. Current position highlighted. Tapping an entry jumps to the first
  matching page. A letter or range with no rows under the active filter renders
  dimmed and is not tappable.
- **Sprite grid**, 6 cols x 3 rows = 18 tiles. Grid area is ~940 x 390 px after
  the rail and margins, so each cell is roughly 156 x 130 px with an 80x80 sprite
  centred in it. **The whole cell is the touch target**, not just the sprite.
  Each tile shows sprite, name, `#dex`. Selected tile gets a 2 px accent border
  in its type colour.
- **Footer**: `↑ TOP`, `PREV`, `NEXT`, `SEARCH`, `TYPE`, `SHADOWS`, `SORT`, and a
  range readout (`51-68 of 1114`) — not a page number, since the window is
  offset-based and need not be page-aligned.

`PORT NOTES` and the on-screen serial-command hints are **deleted**. That space
becomes grid area.

### Detail card

Keep all five existing pages of data. Replace `PAGE -` / `PAGE +` stepping
through anonymous pages with **named tabs**: `ENTRY`, `STATS`, `MOVES`,
`MATCHUPS`, `EVO`. Any page is one tap away instead of up to four. The 320x320
hero sprite replaces the generic pokeball.

`POKEDEX_DETAIL_PAGE_COUNT` stays 5; only the navigation control changes.

### Touch

The grid, the jump rail and the detail tabs are new hit regions and must be added
to `handleTouchMapped`. `POKEDEX_TOUCH_AUTO_REMAP` stays enabled during bring-up
and its existing behaviour is unchanged. A grid tile's touch target is its full
~156 x 130 px cell; rail entries get the full 40 px rail width and split the rail
height evenly.

No long-press or gesture handling — tap only. Out of scope.

## Feature flags

Two new flags, both defaulting to `0` in `shared/CrowPanelShared/AppConfig.h`
under `#ifndef`:

- `USE_POKEDEX_SPRITES`
- `USE_POKEDEX_AUDIO`

All gated code lives under `projects/15-pokedex-panel/`, which is a translation
unit that includes `ProjectConfig.h` — so unlike `USE_DISPLAY` or `USE_WIFI`,
these two never need to reach a shared library `.cpp` and require no
`compiler.cpp.extra_flags` plumbing to work. They still need `AppConfig.h`
defaults so a build with them unset compiles, and `check-flag-matrix.sh` still
passes them as `-D` defines because the matrix must exercise combinations without
editing `ProjectConfig.h`. No `compiler.c.extra_flags` is needed — this project
has no flag-gated C.

Sprites and audio are independent — neither may take the other down, and the
existing `display-sd-pokedex` behaviour must survive with both new flags off.

New rows required in `scripts/check-flag-matrix.sh`:

| Label | Flags |
|---|---|
| `pokedex-sprites` | `-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1` |
| `pokedex-audio` | `-DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_AUDIO=1` |
| `pokedex-expo-full` | `-DUSE_DISPLAY=1 -DUSE_SD_POKEDEX=1 -DUSE_POKEDEX_SPRITES=1 -DUSE_POKEDEX_AUDIO=1` |

The existing `sd-pokedex` and `display-sd-pokedex` rows stay. A combination is
only supported once it has a green row here.

## Serial commands

Existing commands keep working. `help`, `status`, `history` keep answering at
115200 baud with Newline endings, and no output line exceeds 95 characters.

Additions:

- `grid [page]` — print the current grid page as text
- `letter <a-z>` — jump to a letter (switches to A-Z order if not already in it)
- `sort [dex|name]` — set browse order
- `filter [type|none]` — set or clear the type filter
- `shadows [on|off]` — toggle shadow visibility
- `sprite <entry_id>` — report decode result and timing for one sprite
- `audio [on|off|next]` — BGM control, only when `USE_POKEDEX_AUDIO=1`

`status` gains index row count, sprite cache hit rate, and audio state.

## Error handling

Every failure degrades to something visible rather than silent or fatal.

| Failure | Behaviour |
|---|---|
| No SD card | existing mock catalog path, visible reason in status (unchanged) |
| `index.csv` missing or empty | existing mock fallback (unchanged) |
| PSRAM allocation for index fails | fall back to the current linear-scan browse, log the reason, stay usable |
| PSRAM allocation for the name-order array fails | dex order only, `SORT` disabled with a visible reason, rail stays in dex mode |
| Sprite file missing | generic pokeball for that tile |
| Sprite header invalid | generic pokeball, log entry_id once |
| Sprite cache allocation fails | decode without caching, accept slower paging |
| WAV missing or wrong format | audio disabled, status says why, UI unaffected |
| Detail JSON malformed | existing behaviour (unchanged) |

## Testing

Host-side, no board required:

- `PokedexIndex` parse and query fixtures against a checked-in slice of
  `index.csv`: paging boundaries in both orders, every order x filter
  combination, a filter matching zero rows, letter jump for a letter with no
  entries, dex jump past the highest dex, first and last page, shadow filter
  counts, and a name-order jump while the type filter is active.
- BMP decode fixture (synthesised in test code, no binary fixture file): one known-good sprite decoded and compared to expected
  RGB565 output; one truncated and one wrong-bpp file must be rejected without
  reading past the buffer.

On-target:

- `./scripts/check-flag-matrix.sh` green on all new rows.
- Flash with `CTAGS_WORKAROUND=1 EXTRA_FLAGS="..." ./scripts/upload-project.sh`.
- Measure and record grid page repaint time and sprite decode time.
- Confirm audio is audible, then confirm the amp-off state is genuinely silent.

## Proof state

Everything in this spec lands as **compile-ready**. Nothing advances to
`uploaded` or `field-proven` in `docs/full-port-proof-matrix.md` until observed
on a real CrowPanel, and each stage's row is updated only against what the
session log actually shows. `NOT HARDWARE-VERIFIED` comments in new source stay
until the matching stage is observed working.

## Out of scope

- Attract / idle mode (operator-driven, not a kiosk)
- Long-press and gesture handling
- Variant grouping under a base entry (rejected in favour of the shadow filter)
- Extracting `PipBoyMedia`'s WAV path into `shared/` — worth doing later, not
  part of this pass
- Any change to `PokedexCatalog`'s public API or its mock data
- Sort orders beyond `DEX` and `A-Z` (no sorting by type, stats, or generation)
- Generation-labelled rail buckets (`Gen I`, `Gen II`) — the dex rail uses plain
  hundreds, which needs no generation boundary table
