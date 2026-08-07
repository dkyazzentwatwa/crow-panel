# Inkwell

Portrait e-ink-style ebook reader for the CrowPanel Advanced 7-inch ESP32-P4.
Under construction.

> This is Project 25 in the [CrowPanel Arduino suite](../../README.md). It
> stays under `in-progress/` until a real reading experience is observed on a
> panel.

## Status

**Serial reader, compile-ready.** The text/EPUB pipeline (TXT/Markdown/
XHTML parsers, EPUB container walk, paginator) is wired up end to end and
driven entirely over Serial. `LibraryStore` has two backends behind one
interface: the default mock (three embedded sample books — one TXT, one
Markdown exercising every supported block, one 3-chapter EPUB; positions
don't survive a reboot) and, behind `USE_INKWELL_SD` (off by default), a
real SD-backed `/books` library with a metadata cache and on-card reading
positions — **compile-verified only, never run against a real card** (see
[TECHNICAL.md](TECHNICAL.md)). Either way books page through the same
`books` / `open` / `page` / `next` / `prev` / `goto` / `toc` / `chapter` /
`font` / `spacing` / `margin` / `close` commands. Still no display or touch
— pages render as plain text to Serial. The host test suite is at 501
checks. A full walkthrough (screenshots, wiring, the actual portrait
render) is still Task 14.

See the [technical reference](TECHNICAL.md).

## Serial commands

At 115200 baud, Newline line ending:

- `help` — list commands
- `status` — reader status: open book, chapter/page, layout settings
- `history` — recent event history
- `books` — list the library
- `open N` — open a library book by index
- `page` — reprint the current page
- `next` / `prev` — turn the page (crosses chapter boundaries)
- `goto PCT` — jump to an approximate position (0-100)
- `toc` — list the open book's table of contents
- `chapter N` — jump to TOC entry N
- `font 1-3`, `spacing 100|115|130`, `margin 32|48|64` — re-layout settings
- `close` — save position and return to the library

## Roadmap

Later tasks add a portrait DSI render path and touch page-turn UI. See the
Inkwell design and implementation plan docs for the full sequence.
