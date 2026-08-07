# 25 — Inkwell Reader

> This is Project 25 in the CrowPanel Arduino suite: a portrait,
> Kindle-style ebook reader with an e-ink aesthetic — on an IPS LCD that
> is honest about imitating one.

Inkwell reads **TXT, Markdown, and EPUB** books. All three formats feed one
pipeline (`InkBook` → `Paginator`), so the serial demo, the host test
suite, and the touch UI all exercise the same shipping code.

## Status

Everything below is **compile-ready only** — nothing has run on a real
panel yet. The serial mock (all flags off) is the fully working surface:
three embedded sample books (a TXT short story, a Markdown feature demo,
and a 3-chapter in-RAM EPUB) with page turns, TOC jumps, font/spacing/
margin re-layout, and resume. The host suite proves the core:

```bash
./scripts/test-inkwell.sh   # 501 g++ checks: parsers, EPUB, paginator, facade
```

Hardware paths behind flags: `USE_DISPLAY` (portrait panel + touch UI),
`USE_INKWELL_SD` (real `/books` library + positions + covers). See the
[technical reference](TECHNICAL.md) for bring-up order and risks.

## Serial demo (115200, Newline)

```
> books
library:
0: The Lighthouse Year by  [TXT] 2659 bytes, 0% read
1: Inkwell Markdown Sampler by  [MD] 1053 bytes, 0% read
2: The Inkwell Sampler by Project 25 [EPUB] 2567 bytes, 0% read
> open 2
----------------------------------------------
# The Blank Page
The morning the press arrived, the whole village came ...
-- The Inkwell Sampler · ch 1/3 · p 1/1 · 0% --
> toc
0: The Blank Page
1: The Marginalia
2: The Long Shelf
> chapter 1
> next
> font 3
> close
```

Commands: `books`, `open <n>`, `page`, `next`, `prev`, `goto <pct>`,
`toc`, `chapter <n>`, `font 1-3`, `spacing 100|115|130`,
`margin 32|48|64`, `close`, plus the standard `help` / `status` /
`history`.

## Touch UI (`USE_DISPLAY=1`, portrait)

- **Library** — 2×2 card grid; covers appear after a book has been opened
  once (see TECHNICAL.md); tap a card to resume where you left off.
- **Reading** — tap right 40% next page, left 25% previous, center opens
  the HUD (progress scrubber, Library / Contents / Aa, page-flash toggle,
  brightness). Optional e-ink-style invert flash on page turns.
- **Contents** — chapter list from the EPUB TOC or Markdown headings;
  unresolved entries are greyed.
- **Aa** — font size (3 FreeSerif steps), line spacing, margins, flash.

## SD layout (`USE_INKWELL_SD=1`)

```
/books/               your .txt / .md / .epub files (FAT32 card)
/books/.inkwell/      Inkwell's own files: catalog cache, <book>.pos
                      positions, page-index sidecars, cover thumbs
```
