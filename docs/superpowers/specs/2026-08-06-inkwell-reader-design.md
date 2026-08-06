# Inkwell — portrait e-ink-style ebook reader — Design

Date: 2026-08-06
Status: approved for planning
Project: `in-progress/25-inkwell` (project 25; 24 is Acid Glass)

## What it is

A Kindle-style reading device in portrait orientation on the CrowPanel's
1024x600 LCD. The panel is IPS, not e-ink, so this is an **e-ink aesthetic**:
warm paper background, near-black serif text, minimal chrome, full-page
redraws only, an optional "invert flash" page-turn effect that mimics an
e-ink refresh, and backlight level as a first-class reading setting.

A reader is the ideal app for this panel's single-framebuffer DSI display:
pages redraw once every few seconds, so the no-animation constraint that
hurts other projects is the product here. No offscreen canvas needed for the
reading surface.

Books live on SD under `/books` as `.txt`, `.md`, or `.epub` (all three in
v1 — decided, not deferred). Scope decisions made during brainstorming:

- **In v1:** library browser with cover thumbnails, page turns, resume,
  font & layout settings (the Kindle "Aa" menu), chapter/TOC navigation.
- **Out of v1:** bookmarks beyond resume-position, highlights, in-book
  search, dictionaries, images inside book content (skipped with a
  placeholder rule), and any network features.

## UI model

Portrait, 600 wide x 1024 tall logical canvas.

- **Library** — grid of cover thumbnails (2 columns) with title, author,
  progress %. EPUB covers come from the package metadata; TXT/MD and
  cover-less EPUBs get a generated placeholder card (title set in serif on
  a paper card). Tap opens at the saved position.
- **Reader** — text page with a thin footer line: title (truncated), page
  x/y in chapter, book %. Tap zones: right ~40% next page, left ~25%
  previous page, center opens the HUD. Horizontal swipes also turn pages.
- **HUD overlay** (center tap) — progress scrubber, TOC button, Aa button,
  brightness slider, back-to-library. Drawn over the page; closing redraws
  the page.
- **TOC view** — chapter list from the EPUB TOC (nav doc, NCX fallback) or
  Markdown `#`/`##` headings; tap jumps to the chapter/heading.
- **Aa menu** — font size (3 steps of FreeSerif), line spacing, margins,
  invert-flash toggle. Any layout change triggers re-pagination (see below).

## Architecture

```
in-progress/25-inkwell/
  25-inkwell.ino          serial commands, setup/loop (functions before use)
  config/ProjectConfig.h  flag + tuning overrides
  src/
    InkDoc.h              shared block model (see below)
    TxtParser.{h,cpp}     paragraphs split on blank lines
    MarkdownParser.{h,cpp} reading subset: # ## ### headings, bold/italic,
                          lists, blockquotes, fenced code, hr
    XhtmlParser.{h,cpp}   EPUB chapter XHTML subset: h1-h6, p, em/i,
                          strong/b, blockquote, pre/code, ul/ol/li, hr, br;
                          unknown tags degrade to their text content;
                          named + numeric entity decoding
    EpubBook.{h,cpp}      zip open via vendored miniz, container.xml ->
                          OPF -> spine + metadata + cover, TOC (nav/NCX)
    miniz.{h,c}           vendored single-file miniz (public domain)
    Paginator.{h,cpp}     greedy line layout via a text-measure callback;
                          emits per-chapter page-break byte-offset tables
    LibraryStore.{h,cpp}  SD scan, metadata cache, RGB565 cover-thumb
                          cache, per-book progress files
    LibraryView / ReaderView / TocView / AaMenu   CrowTouch/Widgets UI
    InkTheme.h            paper palette
  README.md / TECHNICAL.md
```

Structural decisions:

- **One intermediate model.** All three parsers emit `InkDoc` blocks — a
  block is {type: h1..h3/body/quote/code/list-item/hr, list depth, and a
  sequence of styled runs {text, bold, italic, mono}}. The paginator and
  renderer never know the source format; EPUB is XHTML chapters feeding
  the same pipe as Markdown. This is the project's "one pipeline".
- **Parsers and miniz compile unconditionally.** They touch no hardware,
  so the mock demo and host tests run the real parsing/pagination code.
  The serial mock embeds a sample TXT, a sample MD, and a tiny EPUB as a
  byte array, exercising the full zip->OPF->XHTML path with all flags off.
  Because miniz.c is never flag-gated, the compiler.c.extra_flags trap
  (flags not reaching .c files) does not apply.
- **Paginator takes a measure callback** (`width(text, style)` +
  line-height). On device it wraps Arduino_GFX `getTextBounds` with the
  FreeSerif fonts; on host it is a fixed-width table. Page-break tests are
  therefore deterministic on the host.
- **Fonts:** GFX FreeSerif at three sizes for body, with Bold / Italic /
  BoldItalic variants for styled runs; FreeMono for code blocks; headings
  use larger FreeSerif sizes. All ship with Adafruit GFX — no new font
  assets.

## Shared-library extension: portrait

`Arduino_DSI_Display` already implements software rotation 0-3 (verified
in the installed GFX Library for Arduino source). `DisplayBringup.cpp`
hardcodes rotation 0 at construction. Change:

- `CrowDisplay::begin()` gains an optional rotation argument (default 0 —
  every existing project unchanged), passed to the `Arduino_DSI_Display`
  constructor.
- A touch remap: the GT911 reports native landscape 1024x600 coordinates;
  `CrowDisplay` remaps them to the logical orientation so
  `touchPoint()`/`touchPoints()` are rotation-consistent for all callers.
- `flush(x,y,w,h)` row-span math operates on framebuffer rows (native
  landscape); the remap for rotated logical rects lives inside
  DisplayBringup, not in projects. (Inkwell mostly full-flushes anyway —
  one page draw per turn.)

This is the repo's first portrait project; the extension is generic and
future portrait projects get it free.

## Pagination and resume (the hard part)

- Pages are computed **per chapter** (EPUB spine item; the whole file is
  one chapter for TXT/MD) as tables of page-start byte offsets into the
  chapter source.
- Lazy: the chapter being opened paginates immediately; neighbor chapters
  paginate in loop idle time. Whole-book page numbers are therefore
  approximate until all chapters are indexed; the footer shows page x/y
  *in chapter* plus book % (byte-based), which is honest and always
  available.
- Break tables persist as sidecar files under `/books/.inkwell/`, keyed by
  source file size + mtime + a settings hash (font size, margins, line
  spacing). Settings change -> new hash -> re-paginate current chapter
  first, rest in background.
- Reading position is stored as (spine index, byte offset of the top of
  the current page) in a small per-book progress file. Byte offsets
  survive re-pagination: after a font change the reader lands on the page
  containing that offset.
- Chapters stream from SD one at a time into PSRAM (32 MB; a chapter is
  trivially small). Never whole-book loads. SD access is Arduino `FS`
  (`SD_MMC`) only — no C stdio mixing (path-namespace invariant).

## Covers

- EPUB covers: JPEG via JPEGDEC (already in the verified set), PNG via
  **PNGdec (new library — add to install-libs.sh + libraries.txt)**.
- Decoded once, scaled to thumbnail size, cached as raw RGB565 files in
  `/books/.inkwell/` so the library opens without re-decoding.
- Decode failure or no cover -> generated placeholder card. Never blocks
  the library.

## Feature flags

- `USE_DISPLAY` (existing) gates all rendering.
- `USE_INKWELL_SD` (new) gates SD_MMC access; follows the per-project SD
  flag convention (USE_ACID_GLASS_SD, USE_SD_POKEDEX). Added to
  `AppConfig.h` under `#ifndef` and to `scripts/check-flag-matrix.sh`
  rows: off/off (mock), display-only, display+SD.
- With flags off the sketch is the standard serial demo: `books`,
  `open <n>`, `next`, `prev`, `goto <pct>`, `toc`, `chapter <n>`,
  `font <1-3>`, `status`, `help`, `history` at 115200/Newline, driving
  the same LibraryStore/Paginator/parsers as the touch UI.
- Registry: add `in-progress/25-inkwell` to `crowpanel_inprogress_projects`.

## Testing

`scripts/test-inkwell.sh` (g++ host suite, seconds, no board):

- Markdown fixtures -> expected InkDoc block streams (headings, styles,
  lists, code fences, edge cases: unterminated emphasis, mixed lists).
- XHTML fixtures -> blocks (entities, nested inline styles, unknown tags,
  self-closed br, attribute noise).
- A tiny fixture EPUB (checked in, few KB) through the real miniz + OPF +
  spine + TOC path, both nav-doc and NCX variants.
- Paginator: deterministic breaks with the mock measurer; resume-offset
  lands on the correct page after a simulated font change; sidecar
  cache key invalidation.
- Malformed inputs: truncated zip, missing container.xml, garbage OPF ->
  error result, no crash.

Compile gates: `compile-all.sh` and new `check-flag-matrix.sh` rows green.

## Proof state and risks

Starts `compile-ready`. Nothing claims hardware behavior until flashed.
The two things that genuinely need hardware proof first:

1. **Portrait rotation** — compile-verified path exists in Arduino_GFX,
   but no repo project has ever run rotation != 0 on this panel. Verify
   text orientation and touch remap before building UI polish on top.
2. **SD_MMC + this project's file patterns** — sidecar writes, many small
   files in `/books/.inkwell/`.

Secondary risks: real-world EPUB variance (the fixture suite plus
degrade-to-text parsing is the defense) and FreeSerif metrics making the
measure callback slow if called per-word naively (mitigation: per-style
glyph-advance caching inside the device measurer if pagination of a
chapter exceeds ~1 s).

## Error handling

- Malformed EPUB/zip: book still lists (filename as title) with an error
  badge; opening shows a message card. No crashes on bad input — the host
  suite covers this.
- Unknown XHTML tags and entities degrade to plain text content.
- Missing sidecar/progress files are recreated silently.
- SD removed mid-read: reader keeps the loaded chapter working, shows an
  error card on the next chapter load; library shows an empty-state card.
