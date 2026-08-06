# Inkwell

Portrait e-ink-style ebook reader for the CrowPanel Advanced 7-inch ESP32-P4.
Under construction.

> This is Project 25 in the [CrowPanel Arduino suite](../../README.md). It
> stays under `in-progress/` until a real reading experience is observed on a
> panel.

## Status

**Scaffold only, compile-ready.** This is just the project skeleton: the
shared Serial UX (`help`, `status`, `history`) and a placeholder `books`
command. No text parsing, no EPUB/plain-text support, and no display
rendering exist yet — those land in later tasks.

## Serial commands

At 115200 baud, Newline line ending:

- `help` — list commands
- `status` — scaffold and proof status
- `history` — recent event history
- `books` — placeholder; prints `library: (empty scaffold — Task 8 adds sample books)`

## Roadmap

Later tasks add a text/EPUB pipeline, a portrait DSI render path, and sample
books. See the Inkwell design and implementation plan docs for the full
sequence.
