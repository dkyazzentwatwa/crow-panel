# Inkwell — Technical Notes

Under construction. `25-inkwell.ino` is a serial mock reader: the shared
`SerialCommandRouter` + `EventLog` + `StatusReport` stack, plus the full
text/EPUB core (`src/TxtParser`, `MarkdownParser`, `XhtmlParser`, `EpubBook`,
`Paginator`, `InkBook`) and `LibraryStore`, which now has two backends behind
one interface (mock samples, or a real SD-backed `/books` library — see
below). No display or touch drivers are wired in yet.

## Proof state

**compile-ready, serial reader.** Verified with `arduino-cli compile` against
the esp32p4 FQBN used by the rest of the repo, in BOTH flag states:

```bash
# mock backend (USE_INKWELL_SD=0, default)
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-path _arduino-build/25-inkwell in-progress/25-inkwell

# SD backend
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-property compiler.cpp.extra_flags=-DUSE_INKWELL_SD=1 \
  --build-path _arduino-build/25-inkwell-sd in-progress/25-inkwell
```

The host test suite (`scripts/test-inkwell.sh`) is at 501 checks covering the
parsers, EPUB container walk, and paginator (`LibraryStore` itself is
Arduino-only and isn't built by the host suite — see its header comment).

**The SD backend (`USE_INKWELL_SD=1`) is compile-verified only. It has never
been run against a real card or board** — there is no SD card in the
environment this was written in. What's actually verified for it:

- Both flag states compile green (commands above).
- `strings`/`nm` on the SD build's `LibraryStore.cpp.o` show the SD-only
  code path is real, not dead: `catalog.txt`, `.pos`, `/books/.inkwell`,
  `SD_MMC`, `heap_caps_malloc`/`heap_caps_free` all appear; none of them
  appear in the mock build's object.
- `_arduino-build/25-inkwell-sd/libraries/` contains `SD_MMC` and `FS`; the
  mock build's `libraries/` does not — confirming real linkage, not just a
  green compile (the `__has_include` trap this repo has hit before).
- Code-level review of every path (mount, scan, catalog cache, position
  sidecars, page-index sidecars, the single whole-book PSRAM buffer).

No claim beyond that. Hardware bring-up (a real card, real files, an actual
mount) is a separate, later step and must not be inferred from this.

Nothing has been uploaded to a board for either backend, and no display
hardware behavior has been observed — pagination and page turns are only
proven against `SerialMeasure`'s fixed per-style metrics, not real glyph
widths.

## Config

`config/ProjectConfig.h`:

- `USE_INKWELL_SD` — real SD card access (off by default; mock-first).
  Consumed by `src/LibraryStore.{h,cpp}` (Task 9).
- `INKWELL_SDMMC_1BIT` — SD_MMC 1-bit bus mode, matching every other
  CrowPanel SD consumer in this repo.
- `INKWELL_BOOKS_DIR` / `INKWELL_CATALOG_DIR` / `INKWELL_CATALOG_PATH` — the
  on-card layout (FS-relative; SD_MMC prepends its own mount point).
- `INKWELL_ROTATION` — portrait rotation for `Arduino_DSI_Display`
  (1 = 90° CW, 3 = 90° CCW; default 1 until hardware bring-up decides)
- `INKWELL_PAGE_W` / `INKWELL_PAGE_H` — logical portrait page geometry
  (600 x 1024)

The rotation/page-geometry flags aren't consumed yet; they exist so the
later display task has an agreed-on place to read from.

## SD backend (Task 9)

On-card layout, all reached through Arduino FS (`SD_MMC`) — never C stdio,
per this repo's "FS prepends the mount point" invariant:

```
/books/                     *.txt, *.md, *.epub (case-insensitive; dotfiles
                             and files over 12MB are skipped at scan time)
/books/.inkwell/catalog.txt name|size|title|author  ('|' in title/author is
                             escaped to '/'; a filename can never contain
                             '|' -- illegal on FAT/exFAT -- so the name field
                             itself is never escaped)
/books/.inkwell/<name>.pos  "spine=N off=N pct=N", rewritten on every
                             page turn (parsed tolerantly; missing/garbled
                             falls back to position 0, never a crash)
/books/.inkwell/<name>.<chapter>.<layouthash>.idx
                             page-start-offset list, one per line, written
                             after layout (v1: write + stale-hash cleanup
                             only; nothing reads these back yet beyond an
                             existence check that skips a redundant write)
```

Design decisions worth knowing before extending this:

- **One SD_MMC owner.** Inkwell is the only SD consumer in this project, so
  `LibraryStore` owns `SD_MMC.begin()`/mount state outright (same
  "don't re-mount a card another subsystem already brought up" guard as
  projects 18 and 22, just with no second subsystem here to hand off to).
- **Whole-book buffer: one at steady state, briefly two during a swap.**
  `bookData()` keeps at most one `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
  buffer alive between calls, and frees the previous one only after a new
  load has already succeeded — a failed read can never take down a book
  that was already open. That means for the moment between "new buffer
  loaded" and "old buffer freed", BOTH books' bytes are resident at once;
  peak PSRAM use during a swap is roughly two books' worth, not one. This is
  why `kMaxBookBytes` is sized at 12MB rather than a larger number (see
  below), and `releaseBookData()` exists to drop the buffer entirely once
  the reader goes back to the library view rather than leaving it pinned.
  The `.ino` additionally calls `book.close()` before ever asking the
  library for a different book's bytes, so `InkBook`/`EpubBook` (which needs
  its buffer to outlive it, not just survive `open()` — see `InkBook.h` and
  `EpubBook.h`) never holds a stale pointer, even momentarily.
- **Catalog cache is a cache, not a source of truth.** It's rebuilt from
  exactly what's registered on every scan (stale rows for removed/renamed
  files are dropped), and a torn/corrupt catalog just makes the next scan
  re-open every EPUB instead of failing — same self-healing story as the
  `.pos` and `.idx` sidecars, which is why none of them use the
  temp+rename+backup machinery `DeskStorage` (project 18) uses for its
  index — these are all disposable caches, not user data. Title/author are
  scrubbed (`catalogSafe()`) at the moment they're first read, not just at
  catalog-write time, so a book's displayed metadata can't drift between the
  scan that first parses its EPUB and a later scan that hits the cache. The
  cache key is name+size, not a modification time (FAT's own mtime
  resolution and Arduino FS's exposure of it aren't reliable enough to lean
  on here) — a book edited in place without changing its byte count keeps
  serving its old cached title/author until the file is renamed or its size
  changes. Considered acceptable: EPUB metadata essentially never changes
  after a file lands on the card, and a rescan is always one delete-and-
  recopy away from picking up new metadata regardless. A rescan that finds
  nothing changed skips rewriting the catalog file entirely (order-
  independent comparison against what's already on disk).
- **`kMaxBooks` is 32, `kMaxBookBytes` is 12MB** — see the comments on both
  in `src/LibraryStore.h` for the memory reasoning: `kMaxBooks` is a
  fixed-table cost per slot; `kMaxBookBytes` is sized so that TWO max-size
  books (the transient swap peak above) is 24MB, leaving 8MB of the 32MB
  PSRAM budget for everything else the app needs. 12MB remains enormous for
  a single ebook.

## What's not here yet

- Display rendering (portrait DSI path) — pages render as text to Serial
- Touch input / page-turn UI
- Reading page-index sidecars back for anything (out of scope for Task 9 —
  see `LibraryStore::writePageIndex()`'s doc comment)

See the design and implementation plan docs for the full task sequence.
