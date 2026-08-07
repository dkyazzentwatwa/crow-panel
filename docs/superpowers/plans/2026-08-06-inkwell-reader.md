# Inkwell (project 25) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A portrait, Kindle-style e-ink-aesthetic ebook reader for the CrowPanel that reads TXT/Markdown/EPUB from SD, with cover-thumbnail library, font settings, TOC navigation, and resume.

**Architecture:** Three parsers (TXT/MD/XHTML) converge on one `InkDoc` block model; a measure-callback `Paginator` turns blocks into pages; an `InkBook` facade wraps formats (EPUB via vendored miniz). All of that compiles unconditionally and is host-tested with g++. Device layers (SD via `USE_INKWELL_SD`, display via `USE_DISPLAY` + new shared portrait-rotation support) sit on top, mock-first.

**Tech Stack:** arduino-cli, esp32:esp32@3.3.8, Arduino_GFX (`Arduino_DSI_Display` rotation 0–3), SD_MMC, vendored miniz, JPEGDEC + PNGdec (covers), vendored Adafruit FreeSerif/FreeMono GFX fonts, g++ host tests.

**Spec:** `docs/superpowers/specs/2026-08-06-inkwell-reader-design.md` — read it first.

**Repo rules that bite here** (from CLAUDE.md — non-negotiable):
- `CTAGS_WORKAROUND=1` on every arduino-cli build; define functions before use in the `.ino`.
- Never wrap a feature-flagged library include in `__has_include`.
- Flags for shared code go through `EXTRA_FLAGS` (compiler.cpp.extra_flags). miniz.c is **never flag-gated**, so the `.c`-flags trap does not apply.
- New flag ⇒ `AppConfig.h` `#ifndef` default 0 **and** rows in `scripts/check-flag-matrix.sh`.
- SD access is Arduino `FS`/`SD_MMC` only — no C stdio paths.
- Proof matrix rows never move past evidence.

**Compile command used throughout** (run from repo root; "baseline compile" means this, green):

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/25-inkwell in-progress/25-inkwell
```

**Host test command used throughout:**

```bash
./scripts/test-inkwell.sh
```

---

## File structure (the whole project at a glance)

```
in-progress/25-inkwell/
  25-inkwell.ino            serial router, setup/loop, mock demo driver
  config/ProjectConfig.h    flag defaults + tuning
  src/
    InkDoc.h                block/run model + style enum        [host-tested]
    TxtParser.{h,cpp}       blank-line paragraphs → blocks      [host-tested]
    MarkdownParser.{h,cpp}  MD subset → blocks                  [host-tested]
    XhtmlParser.{h,cpp}     XHTML subset → blocks + entities    [host-tested]
    miniz.{h,c}             vendored, single-file, unconditional
    EpubBook.{h,cpp}        zip → OPF/spine/metadata/TOC/cover  [host-tested]
    Paginator.{h,cpp}       lines+pages via TextMeasure         [host-tested]
    InkBook.{h,cpp}         format facade + TOC + book %        [host-tested]
    LibraryStore.{h,cpp}    mock samples + SD backend + progress/sidecars
    InkTheme.h              paper palette + font tables (display builds)
    GfxMeasure.{h,cpp}      TextMeasure over Arduino_GFX getTextBounds
    ReaderView.{h,cpp}      page render, footer, tap zones, flash
    LibraryView.{h,cpp}     cover grid
    TocView.{h,cpp}         chapter list
    AaMenu.{h,cpp}          font/spacing/margins/flash/brightness HUD
    CoverArt.{h,cpp}        JPEGDEC/PNGdec decode + RGB565 thumb cache
    SampleBooks.h           embedded TXT/MD samples + in-RAM fixture EPUB
    fonts/                  vendored FreeSerif*/FreeMono* headers
  test/
    host_main.cpp           g++ test runner (assert-style, like project 18)
  README.md                 demo script / serial walkthrough
  TECHNICAL.md              wiring, proof state, format notes
scripts/test-inkwell.sh     g++ host suite
Modified:
  scripts/project-registry.sh          add in-progress/25-inkwell
  shared/CrowPanelShared/AppConfig.h   USE_INKWELL_SD
  shared/CrowPanelShared/DisplayBringup.{h,cpp}  rotation + touch remap
  scripts/check-flag-matrix.sh         P25 rows
  scripts/install-libs.sh, libraries.txt         PNGdec
  docs/full-port-proof-matrix.md       P25 row (compile-ready)
```

Naming/style: 2-space indent, `#ifndef` guards, PascalCase classes. The host-tested core uses `std::string`/`std::vector` (chapter-scoped, freed on close; keeps Arduino headers out of the exact TUs under test — same rationale as project 18's byte-source parsers). UI/serial layers use Arduino `String` per repo style.

---

## Phase 0 — scaffold

### Task 1: Project scaffold + registry

**Files:**
- Create: `in-progress/25-inkwell/25-inkwell.ino`
- Create: `in-progress/25-inkwell/config/ProjectConfig.h`
- Create: `in-progress/25-inkwell/README.md`, `in-progress/25-inkwell/TECHNICAL.md` (short stubs, expanded in Task 14)
- Modify: `scripts/project-registry.sh` (add to `crowpanel_inprogress_projects`)

- [ ] **Step 1: ProjectConfig.h**

```cpp
#ifndef INKWELL_PROJECT_CONFIG_H
#define INKWELL_PROJECT_CONFIG_H

// Inkwell is mock-first. Real SD and display builds pass these flags through
// compiler.cpp.extra_flags so shared translation units see the same values
// (see CLAUDE.md's three-layer flag rule).
#ifndef USE_INKWELL_SD
#define USE_INKWELL_SD 0
#endif

// Portrait rotation for Arduino_DSI_Display: 1 = 90° CW (USB at bottom),
// 3 = 90° CCW. Hardware bring-up decides which; default 1 until proven.
#ifndef INKWELL_ROTATION
#define INKWELL_ROTATION 1
#endif

// Logical page geometry (portrait).
#ifndef INKWELL_PAGE_W
#define INKWELL_PAGE_W 600
#endif
#ifndef INKWELL_PAGE_H
#define INKWELL_PAGE_H 1024
#endif

#endif  // INKWELL_PROJECT_CONFIG_H
```

- [ ] **Step 2: minimal .ino** — serial router with `help`/`status`/`history` answered by the shared stack, one placeholder `books` command. Functions defined before use (ctags workaround). Pattern:

```cpp
#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

SerialCommandRouter router;
EventLog eventLog;

void cmdBooks(const String &) {
  Serial.println("library: (empty scaffold — Task 8 adds sample books)");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  eventLog.begin("inkwell");
  router.begin(&eventLog);
  router.on("books", "List books in the library", cmdBooks);
  Serial.println("Inkwell — portrait e-ink-style reader (scaffold)");
  router.printHelp();
}

void loop() { router.tick(); }
```

Before writing this, open `in-progress/24-acid-glass-visualizer/24-acid-glass-visualizer.ino` and copy its exact router/EventLog/StatusReport boot idiom (names above are from memory of the shared API — the neighbouring project is authoritative; keep `status`/`history` wired however project 24 does it).

- [ ] **Step 3: registry** — in `scripts/project-registry.sh`, add `in-progress/25-inkwell` after the `24-acid-glass-visualizer` line inside `crowpanel_inprogress_projects()`.

- [ ] **Step 4: baseline compile** — run the compile command from the header. Expected: green.

- [ ] **Step 5: Commit** — `git add in-progress/25-inkwell scripts/project-registry.sh && git commit -m "feat(25): scaffold Inkwell portrait reader (project 25)"`

---

## Phase 1 — host-testable core (TDD throughout)

### Task 2: Host harness + InkDoc + TxtParser

**Files:**
- Create: `in-progress/25-inkwell/src/InkDoc.h`
- Create: `in-progress/25-inkwell/src/TxtParser.h`, `src/TxtParser.cpp`
- Create: `in-progress/25-inkwell/test/host_main.cpp`
- Create: `scripts/test-inkwell.sh` (chmod +x)

- [ ] **Step 1: InkDoc.h** (complete file):

```cpp
#ifndef INKWELL_INK_DOC_H
#define INKWELL_INK_DOC_H

#include <cstdint>
#include <string>
#include <vector>

// The format-neutral document model. TxtParser, MarkdownParser and
// XhtmlParser all emit this; Paginator and the renderer consume only this.
namespace Ink {

enum class BlockType : uint8_t { Body, H1, H2, H3, Quote, Code, ListItem, Rule };

// Render styles the measurer/renderer understand. Headings collapse inline
// styling (a bold word inside an H2 just renders H2).
enum Style : uint8_t {
  kStyleBody = 0, kStyleBold, kStyleItalic, kStyleBoldItalic, kStyleMono,
  kStyleH1, kStyleH2, kStyleH3,
  kStyleCount
};

struct Run {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool mono = false;
};

struct Block {
  BlockType type = BlockType::Body;
  uint8_t listDepth = 0;   // ListItem only (1 = top level)
  bool ordered = false;    // ListItem only
  uint32_t srcOffset = 0;  // byte offset of the block's start in chapter source
  std::vector<Run> runs;
};

inline uint8_t styleFor(const Block &b, const Run &r) {
  switch (b.type) {
    case BlockType::H1: return kStyleH1;
    case BlockType::H2: return kStyleH2;
    case BlockType::H3: return kStyleH3;
    case BlockType::Code: return kStyleMono;
    default: break;
  }
  if (r.mono) return kStyleMono;
  if (r.bold && r.italic) return kStyleBoldItalic;
  if (r.bold) return kStyleBold;
  if (r.italic) return kStyleItalic;
  return kStyleBody;
}

// Concatenated plain text of a block (tests + TOC titles).
inline std::string plainText(const Block &b) {
  std::string out;
  for (const Run &r : b.runs) out += r.text;
  return out;
}

}  // namespace Ink

#endif
```

- [ ] **Step 2: test harness + first failing tests.** `test/host_main.cpp` starts as an assert-runner in project 18's style:

```cpp
// Host tests for Inkwell's format core. No Arduino, no SD, no display:
// these are the EXACT translation units that ship in the firmware.
#include <cassert>
#include <cstdio>
#include <string>
#include "../src/InkDoc.h"
#include "../src/TxtParser.h"

static int checks = 0;
#define CHECK(cond) do { \
  if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
  ++checks; \
} while (0)

static int testTxtParagraphs() {
  auto blocks = Ink::parseTxt("First para line one.\nline two.\n\n\nSecond para.\n");
  CHECK(blocks.size() == 2);
  CHECK(blocks[0].type == Ink::BlockType::Body);
  CHECK(Ink::plainText(blocks[0]) == "First para line one. line two.");
  CHECK(blocks[1].srcOffset == 33);  // offset of 'S' in "Second"
  CHECK(Ink::plainText(blocks[1]) == "Second para.");
  return 0;
}

static int testTxtEdges() {
  CHECK(Ink::parseTxt("").empty());
  CHECK(Ink::parseTxt("\n\n\n").empty());
  auto crlf = Ink::parseTxt("a\r\nb\r\n\r\nc");
  CHECK(crlf.size() == 2);
  CHECK(Ink::plainText(crlf[0]) == "a b");
  auto tabs = Ink::parseTxt("x\ty");
  CHECK(Ink::plainText(tabs[0]) == "x y");
  return 0;
}

int main() {
  if (testTxtParagraphs()) return 1;
  if (testTxtEdges()) return 1;
  std::printf("inkwell host tests: %d checks passed\n", checks);
  return 0;
}
```

- [ ] **Step 3: scripts/test-inkwell.sh** (complete; grows only its source list in later tasks):

```bash
#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for Inkwell (project 25): parsers, EPUB container walk and
# paginator — the code where a silent off-by-one reads as a corrupt book.
# All TUs under test are Arduino-free; the shipping files are the tested files.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/in-progress/25-inkwell"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/inkwell-host"

CXX="${CXX:-g++}"
command -v "$CXX" >/dev/null 2>&1 || { echo "$CXX required" >&2; exit 1; }
mkdir -p "$OUT"

echo "Building Inkwell host tests with $CXX"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$PROJECT/src/TxtParser.cpp" \
  "$PROJECT/test/host_main.cpp" \
  -o "$OUT/inkwell-tests"

"$OUT/inkwell-tests" "$@"
```

`chmod +x scripts/test-inkwell.sh`

- [ ] **Step 4: run, verify it fails to build** (TxtParser.h missing). Expected: compile error.

- [ ] **Step 5: TxtParser.** Header declares `namespace Ink { std::vector<Block> parseTxt(const std::string &src); }`. Implementation rules: split on blank lines (lines that are empty after trimming); join a paragraph's lines with single spaces; collapse runs of spaces/tabs; normalize `\r\n`; each block is `Body` with one Run; `srcOffset` = offset of the paragraph's first non-blank character in `src`.

```cpp
#include "TxtParser.h"

namespace Ink {

std::vector<Block> parseTxt(const std::string &src) {
  std::vector<Block> blocks;
  size_t i = 0, n = src.size();
  while (i < n) {
    while (i < n && (src[i] == '\n' || src[i] == '\r')) ++i;
    if (i >= n) break;
    size_t start = i;
    std::string text;
    bool blank = false;
    while (i < n && !blank) {
      size_t eol = src.find('\n', i);
      if (eol == std::string::npos) eol = n;
      size_t end = eol;
      if (end > i && src[end - 1] == '\r') --end;
      std::string line;
      for (size_t k = i; k < end; ++k) {
        char c = src[k] == '\t' ? ' ' : src[k];
        if (c == ' ' && !line.empty() && line.back() == ' ') continue;
        line += c;
      }
      while (!line.empty() && line.back() == ' ') line.pop_back();
      size_t first = line.find_first_not_of(' ');
      line = first == std::string::npos ? "" : line.substr(first);
      if (line.empty()) blank = true;
      else {
        if (!text.empty()) text += ' ';
        text += line;
      }
      i = eol == n ? n : eol + 1;
    }
    if (!text.empty()) {
      Block b;
      b.srcOffset = (uint32_t)start;
      b.runs.push_back({text, false, false, false});
      blocks.push_back(std::move(b));
    }
  }
  return blocks;
}

}  // namespace Ink
```

- [ ] **Step 6: run tests, verify pass.** `./scripts/test-inkwell.sh` → `inkwell host tests: N checks passed`. If `srcOffset == 33` disagrees, recount the fixture by hand before touching the parser — the test documents the offset contract.

- [ ] **Step 7: Commit** — `git commit -m "feat(25): InkDoc block model + TxtParser with host tests"`

### Task 3: MarkdownParser

**Files:**
- Create: `src/MarkdownParser.h`, `src/MarkdownParser.cpp`
- Modify: `test/host_main.cpp`, `scripts/test-inkwell.sh` (add the .cpp)

API: `namespace Ink { std::vector<Block> parseMarkdown(const std::string &src); }`

Subset (from the spec — nothing more, YAGNI): `#`/`##`/`###` headings (deeper `####+` → H3), `**bold**`, `*italic*` / `_italic_`, `` `code` `` inline, `- `/`* `/`1. ` lists (nesting by 2-space indent, depth capped at 3), `> ` blockquotes, ``` fenced code blocks (no language handling — skip the info string), `---`/`***`/`___` alone on a line → Rule. Everything else is a Body paragraph. No links rendering (write `[text](url)` as just `text`), no images (`![alt](url)` → drop entirely), no tables, no HTML passthrough (strip `<...>` tags, keep text).

- [ ] **Step 1: failing tests first.** Add to `host_main.cpp` (representative — write all of these):

```cpp
static int testMdHeadingsAndRule() {
  auto b = Ink::parseMarkdown("# Title\n\nBody text.\n\n## Sub *head*\n\n---\n");
  CHECK(b.size() == 4);
  CHECK(b[0].type == Ink::BlockType::H1 && Ink::plainText(b[0]) == "Title");
  CHECK(b[1].type == Ink::BlockType::Body);
  CHECK(b[2].type == Ink::BlockType::H2 && Ink::plainText(b[2]) == "Sub head");
  CHECK(b[3].type == Ink::BlockType::Rule);
  return 0;
}

static int testMdInlineStyles() {
  auto b = Ink::parseMarkdown("plain **bold** and *it* and `mono` mix");
  CHECK(b.size() == 1);
  const auto &runs = b[0].runs;
  CHECK(runs.size() == 7);
  CHECK(runs[1].text == "bold" && runs[1].bold && !runs[1].italic);
  CHECK(runs[3].text == "it" && runs[3].italic);
  CHECK(runs[5].text == "mono" && runs[5].mono);
  return 0;
}

static int testMdListQuoteCode() {
  auto b = Ink::parseMarkdown("- one\n- two\n  - nested\n\n> quoted\n\n```\ncode line\n```\n");
  CHECK(b[0].type == Ink::BlockType::ListItem && b[0].listDepth == 1 && !b[0].ordered);
  CHECK(b[2].listDepth == 2);
  CHECK(b[3].type == Ink::BlockType::Quote && Ink::plainText(b[3]) == "quoted");
  CHECK(b[4].type == Ink::BlockType::Code && Ink::plainText(b[4]) == "code line");
  return 0;
}

static int testMdEdges() {
  // Unterminated emphasis renders literally; links keep text; images drop.
  auto b = Ink::parseMarkdown("a **broken\n\n[text](http://x) ![img](y)\n\n1. first\n2. second");
  CHECK(Ink::plainText(b[0]) == "a **broken");
  CHECK(Ink::plainText(b[1]) == "text");
  CHECK(b[2].ordered && b[2].type == Ink::BlockType::ListItem);
  return 0;
}
```

- [ ] **Step 2: run, verify failure** (missing symbol).

- [ ] **Step 3: implement.** Two layers in `MarkdownParser.cpp`:

*Block scan* — walk lines, classify: fence toggle (```` ``` ````; inside a fence every line appends to one Code block, one block per fence), heading (`^#{1,6} `), rule (line is only `-`/`*`/`_` ×3+), quote (`^> ` prefix stripped, consecutive quote lines join), list item (`^\s*([-*]|\d+\.) ` — depth = 1 + indent/2, capped 3; each item is its own block), else paragraph (consecutive plain lines join with spaces, as TxtParser). Record `srcOffset` at each block's first line start.

*Inline pass* — for every non-Code block, convert its raw text into styled runs with an explicit state machine (complete function, drop it in as-is):

```cpp
namespace {

void emit(std::vector<Ink::Run> &runs, std::string &buf, bool b, bool i, bool m) {
  if (buf.empty()) return;
  runs.push_back({buf, b, i, m});
  buf.clear();
}

// Inline markdown → runs. Emphasis must close within the block or it is
// emitted literally (testMdEdges pins this).
std::vector<Ink::Run> inlineRuns(const std::string &s) {
  std::vector<Ink::Run> runs;
  std::string buf;
  bool bold = false, italic = false, mono = false;
  for (size_t i = 0; i < s.size();) {
    if (mono) {  // inside `...`: only a backtick ends it
      if (s[i] == '`') { emit(runs, buf, bold, italic, true); mono = false; ++i; }
      else buf += s[i++];
      continue;
    }
    if (s[i] == '`') {
      if (s.find('`', i + 1) == std::string::npos) { buf += '`'; ++i; continue; }
      emit(runs, buf, bold, italic, false); mono = true; ++i; continue;
    }
    if (s.compare(i, 2, "**") == 0) {
      if (!bold && s.find("**", i + 2) == std::string::npos) { buf += "**"; i += 2; continue; }
      emit(runs, buf, bold, italic, false); bold = !bold; i += 2; continue;
    }
    if (s[i] == '*' || s[i] == '_') {
      char c = s[i];
      if (!italic && s.find(c, i + 1) == std::string::npos) { buf += c; ++i; continue; }
      emit(runs, buf, bold, italic, false); italic = !italic; ++i; continue;
    }
    if (s[i] == '!' && i + 1 < s.size() && s[i + 1] == '[') {  // image: drop
      size_t close = s.find(']', i);
      size_t paren = close == std::string::npos ? std::string::npos : s.find(')', close);
      if (paren != std::string::npos) { i = paren + 1; continue; }
    }
    if (s[i] == '[') {  // link: keep text, drop target
      size_t close = s.find("](", i);
      size_t paren = close == std::string::npos ? std::string::npos : s.find(')', close);
      if (paren != std::string::npos) {
        std::string inner = s.substr(i + 1, close - i - 1);
        for (const Ink::Run &r : inlineRuns(inner))
          { emit(runs, buf, bold, italic, false); runs.push_back(r); }
        i = paren + 1; continue;
      }
    }
    if (s[i] == '<') {  // strip HTML tags, keep text between them
      size_t close = s.find('>', i);
      if (close != std::string::npos && close - i < 64) { i = close + 1; continue; }
    }
    buf += s[i++];
  }
  emit(runs, buf, bold, italic, mono);
  if (runs.empty()) runs.push_back({"", false, false, false});
  return runs;
}

}  // namespace
```

Note the unterminated-emphasis rule: an opener with no closer ahead in the block is literal text.

> **AMENDED during execution (commit 9851e75):** review probing showed the no-closer rule alone does NOT protect `5 * 3` prose or `snake_case_name` from italicizing. The shipped `inlineRuns` intentionally diverges from the verbatim block above by adding flanking rules: an opener needs a non-space char after the marker, a closer needs a non-space char before it, and `_` is literal when intra-word (alnum both sides). Do not "restore" the verbatim version — the flanking behavior is pinned by tests in `test/host_main.cpp`.
>
> **AMENDED again (commit 245b6ab):** two more defects in the verbatim block, fixed in the shipped version: (1) the unbounded `find()` lookaheads for `](`/`)`/`]`/`>` were O(n²) per block — measured ~18 s for a 512 KB pathological single-block input — and are now window-bounded (512 bytes for links/images, 64 for HTML tags); (2) recursed link-text runs dropped the enclosing bold/italic/mono state, now OR-ed back in. Both pinned by tests. Also note `normalizeLine` maps lone `\r` to space like TxtParser.

- [ ] **Step 4: run tests until green.** Adjust only the implementation, not the pinned expectations.

- [ ] **Step 5: Commit** — `feat(25): Markdown subset parser (headings, styles, lists, quotes, fences)`

### Task 4: XhtmlParser

**Files:**
- Create: `src/XhtmlParser.h`, `src/XhtmlParser.cpp`
- Modify: `test/host_main.cpp`, `scripts/test-inkwell.sh`

API: `namespace Ink { std::vector<Block> parseXhtml(const std::string &src); }` — input is one EPUB chapter document; output starts at `<body>` (if present; otherwise whole input).

Tag subset: `h1`→H1, `h2`→H2, `h3`–`h6`→H3, `p`/`div` paragraph breaks, `blockquote`→Quote (whole subtree), `pre`→Code (whitespace preserved, one block per `<pre>`), `li`→ListItem (depth = `ul`/`ol` nesting, `ordered` from nearest list ancestor), `hr`→Rule, `br`→space, `em`/`i`→italic, `strong`/`b`→bold, `code`/`tt`→mono. `script`/`style`/`head` subtrees dropped. Every other tag is transparent (text flows through). Entities: `&amp; &lt; &gt; &quot; &apos; &nbsp;`(→space)` &mdash;`(→`--`)` &ndash;`(→`-`)` &hellip;`(→`...`), `&#NNN;`/`&#xHH;` → UTF-8 encode (multi-byte fine; FreeSerif renders ASCII, others pass through as-is and draw as blanks — documented degrade). Whitespace outside `<pre>` collapses like HTML.

- [ ] **Step 1: failing tests.** Add (write all):

```cpp
static int testXhtmlBasics() {
  auto b = Ink::parseXhtml(
      "<html><head><title>x</title><style>p{}</style></head><body>"
      "<h1>Chapter &amp; One</h1><p>Hello <em>world</em>, <strong>bold</strong>.</p>"
      "<hr/><blockquote><p>quoted</p></blockquote></body></html>");
  CHECK(b.size() == 4);
  CHECK(b[0].type == Ink::BlockType::H1 && Ink::plainText(b[0]) == "Chapter & One");
  CHECK(b[1].runs.size() >= 4 && b[1].runs[1].italic && b[1].runs[3].bold);
  CHECK(b[2].type == Ink::BlockType::Rule);
  CHECK(b[3].type == Ink::BlockType::Quote);
  return 0;
}

static int testXhtmlListsPreEntities() {
  auto b = Ink::parseXhtml(
      "<body><ul><li>one</li><li>two<ol><li>inner</li></ol></li></ul>"
      "<pre>  keep   spaces</pre>"
      "<p>a&nbsp;b &#65; &mdash; &hellip; &unknown; end</p></body>");
  CHECK(b[0].type == Ink::BlockType::ListItem && b[0].listDepth == 1);
  CHECK(b[2].listDepth == 2 && b[2].ordered);
  CHECK(b[3].type == Ink::BlockType::Code && Ink::plainText(b[3]) == "  keep   spaces");
  CHECK(Ink::plainText(b[4]) == "a b A -- ... &unknown; end");
  return 0;
}

static int testXhtmlDegrade() {
  // Unknown tags transparent; attribute noise ignored; unclosed input survives.
  auto b = Ink::parseXhtml("<body><p class=\"x\" id='y'><span>te</span>xt<p>next");
  CHECK(b.size() == 2);
  CHECK(Ink::plainText(b[0]) == "text");
  CHECK(Ink::plainText(b[1]) == "next");
  return 0;
}
```

- [ ] **Step 2: run, verify failure.**

- [ ] **Step 3: implement.** Single forward scan, no DOM. State: `bold/italic/mono` depth counters (nesting-safe), list stack (`ul|ol` entries), `inPre`, `inQuote`, `dropDepth` (inside script/style/head), current block builder + `flushBlock()`. Tokenizer: at `<`, read to `>` (tolerate missing `>` at EOF by treating rest as text); lowercase the tag name; detect `/` close and self-close; ignore attributes wholesale except none needed in v1. On text: decode entities; collapse whitespace unless `inPre`. Block starts: `h1..h6/p/div/li/pre/blockquote-child` set the pending type; text arriving with no open block opens a Body block (naked text). `srcOffset` = byte offset of the tag/text that opened the block. Keep the entity decoder its own function:

```cpp
// Appends the decoded entity to out; returns chars consumed (0 = not an entity,
// caller emits the '&' literally).
size_t decodeEntity(const std::string &s, size_t i, std::string &out);
```

with a table for the named set and numeric parsing for `&#NNN;`/`&#xHH;` (UTF-8 encode code points ≤ 0xFFFF, else emit `?`). Unknown named entities (test pins `&unknown;`) emit literally.

- [ ] **Step 4: run until green.**
- [ ] **Step 5: Commit** — `feat(25): XHTML subset parser with entity decoding for EPUB chapters`

### Task 5: vendored miniz + EpubBook

**Files:**
- Create: `src/miniz.h`, `src/miniz.c` (vendored)
- Create: `src/EpubBook.h`, `src/EpubBook.cpp`
- Modify: `test/host_main.cpp`, `scripts/test-inkwell.sh` (add `miniz.c` compiled as C via `gcc -c` or just let g++ build it — compile with `-x c` off; miniz compiles fine as C++ too, but keep it `.c` and add a separate `$CC -c` step mirroring how the firmware builds it)

- [ ] **Step 1: vendor miniz.** Download the two-file amalgamation (public domain/MIT, richgel999/miniz release 3.0.2): `miniz.c` + `miniz.h` into `src/`. Add a 5-line provenance comment at the top of each (project, version, URL, license, "vendored unmodified"). Disable what we don't need with defines *in EpubBook.cpp before including* — do NOT edit the vendored file:
  `#define MINIZ_NO_STDIO`, `#define MINIZ_NO_TIME`, `#define MINIZ_NO_ARCHIVE_WRITING_APIS` must NOT be set globally (tests use the writer) — instead compile miniz.c unmodified and rely on the linker to drop unused code. Verify after the firmware compile in Step 6 that `miniz.c.o` exists and is non-trivial in `_arduino-build/25-inkwell` (retro-go lesson: **verify by object size, not exit status**).

- [ ] **Step 2: failing tests, fixture built in memory with miniz's writer** — no binary fixtures in git, no python dependency:

```cpp
#include "../src/EpubBook.h"
#include "../src/miniz.h"

// Builds a minimal but structurally honest EPUB in memory.
static std::string buildFixtureEpub(bool navToc) {
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 16 * 1024);
  auto add = [&](const char *name, const std::string &data) {
    mz_zip_writer_add_mem(&z, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION);
  };
  add("mimetype", "application/epub+zip");
  add("META-INF/container.xml",
      "<?xml version=\"1.0\"?><container><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>");
  std::string opf =
      "<?xml version=\"1.0\"?><package><metadata>"
      "<dc:title>Fixture Book</dc:title><dc:creator>Test Author</dc:creator>"
      "<meta name=\"cover\" content=\"cov\"/></metadata><manifest>"
      "<item id=\"c1\" href=\"ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"c2\" href=\"ch2.xhtml\" media-type=\"application/xhtml+xml\"/>"
      "<item id=\"cov\" href=\"cover.jpg\" media-type=\"image/jpeg\"/>";
  if (navToc)
    opf += "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>";
  else
    opf += "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  opf += "</manifest><spine";
  if (!navToc) opf += " toc=\"ncx\"";
  opf += "><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>";
  add("OEBPS/content.opf", opf);
  add("OEBPS/ch1.xhtml", "<html><body><h1>One</h1><p>First chapter text.</p></body></html>");
  add("OEBPS/ch2.xhtml", "<html><body><h1>Two</h1><p>Second chapter text.</p></body></html>");
  add("OEBPS/cover.jpg", std::string("\xFF\xD8\xFF\xE0 fake jpeg", 14));
  if (navToc)
    add("OEBPS/nav.xhtml",
        "<html><body><nav epub:type=\"toc\"><ol>"
        "<li><a href=\"ch1.xhtml\">Chapter One</a></li>"
        "<li><a href=\"ch2.xhtml#frag\">Chapter Two</a></li></ol></nav></body></html>");
  else
    add("OEBPS/toc.ncx",
        "<ncx><navMap><navPoint><navLabel><text>Chapter One</text></navLabel>"
        "<content src=\"ch1.xhtml\"/></navPoint><navPoint><navLabel>"
        "<text>Chapter Two</text></navLabel><content src=\"ch2.xhtml\"/>"
        "</navPoint></navMap></ncx>");
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  std::string out((char *)buf, size);
  mz_zip_writer_end(&z);
  mz_free(buf);
  return out;
}

static int testEpubOpen(bool navToc) {
  std::string zipData = buildFixtureEpub(navToc);
  Ink::EpubBook book;
  CHECK(book.open((const uint8_t *)zipData.data(), zipData.size()));
  CHECK(book.title() == "Fixture Book");
  CHECK(book.author() == "Test Author");
  CHECK(book.chapterCount() == 2);
  std::string x;
  CHECK(book.chapterXhtml(1, x) && x.find("Second chapter") != std::string::npos);
  CHECK(book.toc().size() == 2);
  CHECK(book.toc()[0].title == "Chapter One" && book.toc()[0].spineIndex == 0);
  CHECK(book.toc()[1].spineIndex == 1);  // href fragment stripped
  std::string cover, mediaType;
  CHECK(book.coverImage(cover, mediaType) && mediaType == "image/jpeg");
  return 0;
}

static int testEpubMalformed() {
  Ink::EpubBook b1, b2, b3;
  CHECK(!b1.open((const uint8_t *)"not a zip", 9));
  std::string zipData = buildFixtureEpub(true);
  CHECK(!b2.open((const uint8_t *)zipData.data(), zipData.size() / 2));  // truncated
  // zip with no container.xml
  mz_zip_archive z; memset(&z, 0, sizeof(z));
  mz_zip_writer_init_heap(&z, 0, 1024);
  mz_zip_writer_add_mem(&z, "hello.txt", "hi", 2, 0);
  void *buf = nullptr; size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&z, &buf, &size);
  CHECK(!b3.open((const uint8_t *)buf, size));
  mz_zip_writer_end(&z); mz_free(buf);
  return 0;
}
```

- [ ] **Step 3: run, verify failure.**

- [ ] **Step 4: EpubBook.h** (complete interface):

```cpp
#ifndef INKWELL_EPUB_BOOK_H
#define INKWELL_EPUB_BOOK_H

#include <cstdint>
#include <string>
#include <vector>

namespace Ink {

struct TocEntry {
  std::string title;
  int spineIndex = -1;  // -1 if the href didn't resolve to a spine item
};

// Reads an EPUB from a memory buffer (the whole .epub lives in PSRAM on
// device — 32 MB makes that the simple, robust choice). The buffer must
// outlive the EpubBook.
class EpubBook {
 public:
  bool open(const uint8_t *data, size_t size);  // false = not a usable EPUB
  void close();
  const std::string &title() const { return title_; }
  const std::string &author() const { return author_; }
  size_t chapterCount() const { return spineHrefs_.size(); }
  uint32_t chapterSize(size_t i) const;          // compressed entry size (for book %)
  bool chapterXhtml(size_t i, std::string &out); // extract chapter i
  const std::vector<TocEntry> &toc() const { return toc_; }
  bool coverImage(std::string &out, std::string &mediaType);

 private:
  bool readEntry(const std::string &name, std::string &out);
  void parseOpf(const std::string &opf, const std::string &opfDir);
  void parseNavToc(const std::string &navXhtml);
  void parseNcxToc(const std::string &ncx);
  int spineIndexForHref(const std::string &href) const;
  // pimpl-free: keep the mz_zip_archive in an opaque aligned buffer so the
  // header stays miniz-free (host tests include miniz.h themselves).
  alignas(8) unsigned char zip_[128];
  bool zipOpen_ = false;
  std::string opfDir_;
  std::string title_, author_;
  std::vector<std::string> spineHrefs_;
  std::string coverHref_, coverMedia_;
  std::vector<TocEntry> toc_;
};

}  // namespace Ink

#endif
```

(Static-assert in the .cpp that `sizeof(mz_zip_archive) <= sizeof(zip_)`; if it fires, grow the buffer.)

> **AMENDED during execution (commit d8c72a4):** quality-review found `EpubBook` had no destructor and was copyable, and `zip_` is a raw `mz_zip_archive` holding a heap-allocated `m_pState` -- a copy shares that pointer with the original, so destroying (or reusing) either one leaves the other pointing at freed memory (ASan-confirmed use-after-free), and with no destructor at all, every `open()` leaked its `mz_zip_reader_end()` cleanup (~30 KB per open, measured on a 400-chapter book). The shipped `EpubBook.h` diverges from the verbatim block above by adding `EpubBook() = default;`, `~EpubBook() { close(); }`, and `EpubBook(const EpubBook &) = delete;` / `operator=` likewise deleted. Do not "restore" the verbatim version -- non-copyability is enforced and relied on by `test/host_main.cpp`. `zip_` is also `mutable` (chapterSize() is logically const but miniz's reader calls need a non-const pointer) and zero-initialized (`= {}`).

- [ ] **Step 5: EpubBook.cpp.** Implementation notes that matter:
  - `open()`: `mz_zip_reader_init_mem`; read `META-INF/container.xml`; pull `full-path` attribute with a tiny helper `attrValue(xml, tag, attr)` (find `<rootfile`, then `full-path="..."` — single/double quotes both); derive `opfDir_` (everything up to last `/`, may be empty); read + `parseOpf`.
  - `parseOpf()`: scan `<item ` tags → map id→(href, media-type, properties). Scan `<itemref ` idrefs in order → `spineHrefs_` (resolve relative to `opfDir_`). `dc:title`/`dc:creator` = text between those tags (tolerate namespace prefixes by searching for `:title>` too). Cover: item with `properties` containing `cover-image`, else `<meta name="cover" content="ID"/>` → that id's href. TOC: item with `properties` containing `nav` → read + `parseNavToc`; else spine `toc="ID"` attr → that id's href → `parseNcxToc`.
  - `parseNavToc()`: within the first `<nav`…`</nav>`, each `<a href="X">TITLE</a>` → entry; strip `#fragment`; title = entity-decoded inner text (reuse `decodeEntity` from XhtmlParser — export it in `XhtmlParser.h` as `Ink::decodeEntities(const std::string&)`).
  - `parseNcxToc()`: each `<navPoint>` → `<text>` content + `<content src="X"/>`.
  - `spineIndexForHref()`: exact match after stripping fragment and normalizing `./`. No match → -1 (entry kept; UI just won't jump).
  - All parse failures degrade: missing metadata → empty strings; missing TOC → empty vector; `open()` only returns false for unreadable zip / missing container.xml / missing OPF.
  - Add `#include` of miniz with `extern "C"` guard in the .cpp only.
  - Update `scripts/test-inkwell.sh`: compile `miniz.c` with `${CC:-gcc} -O2 -c` into `$OUT/miniz.o`, add `EpubBook.cpp XhtmlParser.cpp` and `$OUT/miniz.o` to the link. (Keep `-Wall -Wextra -Werror` off the miniz object only.)

- [ ] **Step 6: run host tests until green. Then run the baseline firmware compile** and verify `miniz.c.o` object size is > 50 KB in `_arduino-build/25-inkwell` (`find _arduino-build/25-inkwell -name 'miniz*.o' -exec ls -l {} \;`).

- [ ] **Step 7: Commit** — `feat(25): vendored miniz + EpubBook container/OPF/TOC/cover parsing`

### Task 6: Paginator

**Files:**
- Create: `src/Paginator.h`, `src/Paginator.cpp`
- Modify: `test/host_main.cpp`, `scripts/test-inkwell.sh`

- [ ] **Step 1: Paginator.h** (complete):

```cpp
#ifndef INKWELL_PAGINATOR_H
#define INKWELL_PAGINATOR_H

#include <cstdint>
#include <string>
#include <vector>
#include "InkDoc.h"

namespace Ink {

// Abstract text metrics so pagination is testable on host and identical on
// device (GfxMeasure wraps Arduino_GFX; tests use fixed per-style widths).
class TextMeasure {
 public:
  virtual ~TextMeasure() = default;
  virtual int16_t textWidth(const std::string &s, uint8_t style) = 0;
  virtual int16_t lineHeight(uint8_t style) = 0;
};

struct LayoutSettings {
  int16_t pageW = 600, pageH = 1024;
  int16_t marginX = 48, marginTop = 40, marginBottom = 64;  // bottom incl. footer
  uint8_t fontStep = 1;         // 0..2 — GfxMeasure/renderer map to font tables
  uint8_t lineSpacingPct = 115; // 100 / 115 / 130
  // Order + fields feed the sidecar-cache key; bump kLayoutVersion on change.
  uint32_t hash() const;
};
constexpr uint8_t kLayoutVersion = 1;

struct LineSeg { std::string text; uint8_t style; int16_t x; };
struct Line {
  std::vector<LineSeg> segs;
  int16_t height = 0;
  uint32_t srcOffset = 0;      // monotonic offset for resume (see spec)
  int16_t indentPx = 0;        // list bullets / quote bar drawn by renderer
  BlockType blockType = BlockType::Body;
  bool firstOfBlock = false;   // renderer draws bullet/quote-bar/rule here
};
struct Page { int firstLine = 0, lineCount = 0; };

// Lays out a whole chapter. Wall-clock is measure-bound: cache glyph advances
// inside the device TextMeasure, not here.
class Paginator {
 public:
  void layout(const std::vector<Block> &blocks, const LayoutSettings &s,
              TextMeasure &m);
  const std::vector<Line> &lines() const { return lines_; }
  const std::vector<Page> &pages() const { return pages_; }
  size_t pageCount() const { return pages_.size(); }
  // Page whose [srcOffset of first line, next page's) range contains off.
  size_t pageForOffset(uint32_t off) const;
  uint32_t pageStartOffset(size_t page) const;

 private:
  std::vector<Line> lines_;
  std::vector<Page> pages_;
};

}  // namespace Ink

#endif
```

- [ ] **Step 2: failing tests.** Mock measurer + pinned behaviors:

```cpp
struct MockMeasure : Ink::TextMeasure {
  // Fixed per-style char widths/heights: deterministic, style-sensitive.
  int16_t widths[Ink::kStyleCount]  = {10, 11, 10, 11, 12, 20, 16, 12};
  int16_t heights[Ink::kStyleCount] = {20, 20, 20, 20, 20, 40, 32, 24};
  int16_t textWidth(const std::string &s, uint8_t st) override {
    return (int16_t)(s.size() * widths[st]);
  }
  int16_t lineHeight(uint8_t st) override { return heights[st]; }
};

static int testPaginatorWrapAndFill() {
  // pageW 600, marginX 48 → content 504 px → 50 body chars/line.
  Ink::LayoutSettings s;
  MockMeasure m;
  std::vector<Ink::Block> blocks;
  Ink::Block b;
  b.srcOffset = 0;
  b.runs.push_back({std::string(120, 'a') + " " + std::string(10, 'b'), false, false, false});
  blocks.push_back(b);
  Ink::Paginator p;
  p.layout(blocks, s, m);
  CHECK(p.lines().size() == 4);            // 50+50+20 a's + word-wrapped b's
  CHECK(p.lines()[0].segs[0].text.size() == 50);
  CHECK(p.pages().size() == 1);
  // Fill many paragraphs → content height 1024-40-64=920 → 46 lines/page at 20px.
  for (int i = 0; i < 30; ++i) { b.srcOffset = 200 + i; blocks.push_back(b); }
  p.layout(blocks, s, m);
  CHECK(p.pages().size() > 1);
  CHECK(p.pages()[0].lineCount <= 46);
  return 0;
}

static int testPaginatorResume() {
  Ink::LayoutSettings s1, s2;
  s2.fontStep = 2;
  MockMeasure m1;
  MockMeasure m2; for (auto &w : m2.widths) w += 6;  // "bigger font"
  std::vector<Ink::Block> blocks;
  for (int i = 0; i < 40; ++i) {
    Ink::Block b; b.srcOffset = i * 100;
    b.runs.push_back({std::string(180, 'x'), false, false, false});
    blocks.push_back(b);
  }
  Ink::Paginator p1; p1.layout(blocks, s1, m1);
  size_t page = p1.pageCount() / 2;
  uint32_t off = p1.pageStartOffset(page);
  Ink::Paginator p2; p2.layout(blocks, s2, m2);
  size_t landed = p2.pageForOffset(off);
  CHECK(p2.pageStartOffset(landed) <= off);
  CHECK(landed + 1 >= p2.pageCount() || p2.pageStartOffset(landed + 1) > off);
  CHECK(s1.hash() != s2.hash());
  CHECK(Ink::LayoutSettings().hash() == s1.hash());
  return 0;
}

static int testPaginatorHardCases() {
  Ink::LayoutSettings s; MockMeasure m; Ink::Paginator p;
  std::vector<Ink::Block> blocks;
  Ink::Block huge;  // one unbreakable 80-char word: must hard-split, not loop
  huge.runs.push_back({std::string(80, 'w'), false, false, false});
  blocks.push_back(huge);
  Ink::Block empty; empty.runs.push_back({"", false, false, false});
  blocks.push_back(empty);
  Ink::Block rule; rule.type = Ink::BlockType::Rule;
  blocks.push_back(rule);
  p.layout(blocks, s, m);
  CHECK(p.lines().size() >= 3);
  CHECK(p.lines()[0].segs[0].text.size() == 50);  // hard split at line width
  CHECK(p.pageCount() == 1);
  p.layout({}, s, m);
  CHECK(p.pageCount() == 1 && p.pages()[0].lineCount == 0);  // empty chapter = 1 blank page
  return 0;
}
```

- [ ] **Step 3: run, verify failure.**

- [ ] **Step 4: implement.** Algorithm (greedy, two phases in one pass):
  - Per block: compute `indentPx` (ListItem: 24 × depth; Quote: 24; else 0), content width = `pageW - 2*marginX - indentPx`. Walk runs word-by-word (split on spaces; Code blocks split on `\n` only, no rewrap). Accumulate segments into the current line while `lineWidthSoFar + wordWidth(+space)` fits; else emit line, start next. A single word wider than the content width hard-splits by characters (binary-search the largest prefix that fits — pinned by the 50-char test). `Line::srcOffset` = block.srcOffset + plain-char count consumed before the line (monotonic within a block, which is all resume needs).
  - Line height = max style height on the line × `lineSpacingPct/100`. Rule blocks emit one 24 px line with no segs. Blocks are separated by a paragraph gap = 40 % of body line height (emitted as extra height on the block's last line, simpler than empty lines).
  - Page fill: pack lines while `y + line.height <= pageH - marginTop - marginBottom`; headings avoid orphaning — if an H1–H3 line would be the last line on a page, push it to the next page.
  - `hash()`: FNV-1a over (kLayoutVersion, pageW, pageH, marginX, marginTop, marginBottom, fontStep, lineSpacingPct) bytes.
  - `pageForOffset`: linear scan of `pageStartOffset` (pages per chapter are few hundred max — no binary search needed, YAGNI).

- [ ] **Step 5: run until green.** The wrap-count expectations (4 lines, 50 chars) are arithmetic from the mock table — if they fail, hand-check the arithmetic before touching the algorithm.

- [ ] **Step 6: Commit** — `feat(25): Paginator — greedy wrap, page fill, resume offsets, settings hash`

### Task 7: InkBook facade

**Files:**
- Create: `src/InkBook.h`, `src/InkBook.cpp`
- Modify: `test/host_main.cpp`, `scripts/test-inkwell.sh`

One object the app talks to, regardless of format:

```cpp
#ifndef INKWELL_INK_BOOK_H
#define INKWELL_INK_BOOK_H

#include "EpubBook.h"
#include "InkDoc.h"

namespace Ink {

enum class Format : uint8_t { Txt, Markdown, Epub };

class InkBook {
 public:
  // data must outlive the InkBook (mock: PROGMEM/static; SD: PSRAM buffer).
  bool open(Format f, const uint8_t *data, size_t size);
  void close();
  Format format() const { return fmt_; }
  const std::string &title() const { return title_; }    // EPUB metadata, else ""
  const std::string &author() const { return author_; }
  size_t chapterCount() const;                            // TXT/MD: 1
  bool loadChapter(size_t i, std::vector<Block> &out);    // parses on demand
  const std::vector<TocEntry> &toc() const { return toc_; }
  // srcOffset of TOC entry i within its chapter (MD headings), 0 for EPUB.
  uint32_t tocOffset(size_t i) const;
  // 0..1000 (permille) position for (chapter, offset) — byte-weighted.
  uint16_t permille(size_t chapter, uint32_t offset) const;

 private:
  Format fmt_ = Format::Txt;
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  EpubBook epub_;
  std::string title_, author_;
  std::vector<TocEntry> toc_;
  std::vector<uint32_t> tocOffsets_;
  std::vector<uint32_t> chapterSizes_;  // for permille
};

}  // namespace Ink

#endif
```

- [ ] **Step 1: failing tests** — MD TOC from headings (`#`/`##` become entries with `spineIndex=0` and correct `tocOffset`), EPUB delegation (title/toc from Task 5 fixture through the facade), `permille(0,0)==0`, `permille(last, lastSize)==1000`, TXT single chapter.
- [ ] **Step 2: implement.** TXT/MD: `loadChapter(0)` runs the parser over `data_`; MD TOC built by a pre-scan parse collecting H1/H2 blocks (title + srcOffset). EPUB: delegate; chapter load = `chapterXhtml` + `parseXhtml`. `permille` uses `chapterSizes_` (EPUB: `chapterSize(i)`; TXT/MD: total size).
- [ ] **Step 3: green, then commit** — `feat(25): InkBook facade — one open-book API across TXT/MD/EPUB`

---

## Phase 2 — mock-first app (all flags off)

### Task 8: sample books + LibraryStore (mock) + serial reader

**Files:**
- Create: `src/SampleBooks.h` — a ~40-line TXT sample, a MD sample exercising every subset feature, and `buildSampleEpub()` (reuse the Task 5 fixture-builder shape, but with 3 chapters of a public-domain-feel short text, built once into a static `std::string` at first use)
- Create: `src/LibraryStore.h`, `src/LibraryStore.cpp`
- Modify: `25-inkwell.ino`

LibraryStore API (mock backend now, SD in Task 9):

```cpp
struct BookEntry {
  String id;        // mock: "sample-txt"; SD: filename
  String title, author;
  Ink::Format format;
  uint32_t bytes = 0;
  uint16_t permille = 0;  // saved progress
};
class LibraryStore {
 public:
  bool begin();                       // mock: registers samples; SD: mounts+scans
  size_t count() const; const BookEntry &entry(size_t) const;
  // Returns a stable pointer/size for InkBook::open. SD: loads into PSRAM.
  bool bookData(size_t i, const uint8_t *&data, size_t &size);
  bool loadPosition(size_t i, uint16_t &spine, uint32_t &offset);
  bool savePosition(size_t i, uint16_t spine, uint32_t offset, uint16_t permille);
};
```

Mock `loadPosition/savePosition` keep an in-RAM table (positions don't survive reboot without SD — that's honest and stated in `status`).

- [ ] **Step 1:** write SampleBooks.h + LibraryStore mock backend.
- [ ] **Step 2:** wire serial commands in the .ino — this is the one-pipeline surface; touch UI reuses these exact functions later. A `MockMeasure` (same table as the tests) + `Paginator` render pages as plain text:
  - `books` — numbered list: title, author, format, %.
  - `open <n>` — open via InkBook, load saved position, print page.
  - `next` / `prev` — page turn (crossing chapter boundaries walks the spine).
  - `goto <pct>` — jump by permille.
  - `chapter <n>` / `toc` — TOC list + jump.
  - `font <1-3>` — set fontStep, re-layout current chapter, re-land via `pageForOffset`, print page.
  - `page` — reprint current page. `status` — book/chapter/page/%/settings.
  Page printing format (pins the renderer contract): 46-char ruler line, then each `Line`'s segs concatenated with style markers `**bold**`, `_it_`, backticks for mono, `#`-prefix for headings, `| ` prefix for quotes, `• ` for list items, `----` for rules, footer line `-- Title · ch 2/5 p 3/12 · 34% --`.
- [ ] **Step 3:** baseline compile green. Manual check: `arduino-cli board list` is NOT needed — no hardware; instead paste a short expected transcript into README.md later. Sanity-run the same flow on host by adding one integration test to `host_main.cpp`: open sample EPUB bytes via InkBook, paginate with MockMeasure, assert page 1 first line text.
- [ ] **Step 4: Commit** — `feat(25): serial mock reader — samples, library, page turns, toc, font steps`

### Task 9: USE_INKWELL_SD backend + progress/sidecar files

**Files:**
- Modify: `shared/CrowPanelShared/AppConfig.h` — add after the other SD flags:

```cpp
#ifndef USE_INKWELL_SD
#define USE_INKWELL_SD 0
#endif
```

- Modify: `src/LibraryStore.cpp` (SD paths under `#if USE_INKWELL_SD`), `25-inkwell.ino` (`sd` status in `status`)

- [ ] **Step 1: SD backend.** `SD_MMC` mount (copy the exact `setPins`/`begin` call from `projects/18-cypher-desk-panel/src/DeskStorage.cpp` — pins from HardwareProfile, and note DeskStorage's single-owner begin()/end() caveat). Scan `/books` for `.txt/.md/.epub` (case-insensitive), cap 64 books, skip dotfiles. EPUB title/author: open just long enough to read OPF metadata at scan time; cache scan results in `/books/.inkwell/catalog.txt` (one line per book: `name|size|title|author`) so rescans only stat.
- [ ] **Step 2: positions + sidecars.** `/books/.inkwell/<name>.pos`: text, `spine=N off=N pct=N`. Page-break sidecar `/books/.inkwell/<name>.<chapter>.<layouthash>.idx`: text, one page-start offset per line; `Paginator` gains nothing — LibraryStore writes `pageStartOffset(i)` after layout and can pre-answer `pageCount` for the footer before layout finishes. Stale-key cleanup: when writing a new hash's sidecar, delete other `.idx` files for the same chapter.
- [ ] **Step 3: compile both ways** — baseline AND `EXTRA_FLAGS="-DUSE_INKWELL_SD=1"` variants of the compile command. Both green.
- [ ] **Step 4: Commit** — `feat(25): SD library backend — /books scan, positions, page-index sidecars`

---

## Phase 3 — display

### Task 10: shared portrait rotation + touch remap

**Files:**
- Modify: `shared/CrowPanelShared/DisplayBringup.h` — `begin()` gains a trailing default arg; mock inline stub updated to match:

```cpp
bool begin(const HardwareProfile &profile, const char *title,
           bool manualFlush = false, uint8_t rotation = 0);
```

- Modify: `shared/CrowPanelShared/DisplayBringup.cpp`

- [ ] **Step 1: pass rotation through.** Store `uint8_t rotation_` in the anon namespace; `beginPanel` passes it to the `Arduino_DSI_Display` constructor instead of the literal `0`. `buildStatusScreen` and `setLine` keep using `kWidth` — change their uses to `gfx->width()` so the boot status screen is correct in any rotation.
- [ ] **Step 2: partial-flush mapping.** `flush(x,y,w,h)` operates on native framebuffer rows. Map the logical rect to a native row span before the existing clamp:

```cpp
  int16_t ny = y, nh = h;
  switch (rotation_) {
    case 1: ny = x; nh = w; break;                    // logical x → native rows
    case 3: ny = kHeight - x - w; nh = w; break;
    case 2: ny = kHeight - y - h; nh = h; break;
    default: break;
  }
```

then run the existing row-clamp/msync logic on `ny/nh`. (Row here = native `kHeight`-direction; the existing code's `y/kHeight/kWidth` names stay.)
- [ ] **Step 3: touch remap** in `sampleTouch()` after reading points:

```cpp
  for (uint8_t i = 0; i < count; ++i) {
    int16_t tx = point.x, ty = point.y;   // native landscape 1024x600
    switch (rotation_) {
      case 1: cachedPoints[i].x = ty;               cachedPoints[i].y = kWidth - 1 - tx; break;
      case 2: cachedPoints[i].x = kWidth - 1 - tx;  cachedPoints[i].y = kHeight - 1 - ty; break;
      case 3: cachedPoints[i].x = kHeight - 1 - ty; cachedPoints[i].y = tx; break;
      default: cachedPoints[i].x = tx;              cachedPoints[i].y = ty; break;
    }
  }
```

Add a `// NOT HARDWARE-VERIFIED: rotation quadrant mapping — verify corners before trusting` comment; the mapping must match whatever `Arduino_DSI_Display` case 1/3 actually does on glass (see `.../GFX_Library_for_Arduino/src/display/Arduino_DSI_Display.cpp` switch blocks — mirror its convention, and hardware bring-up flips 1↔3 if the panel proves the other way).
- [ ] **Step 4: regression gate.** `CTAGS_WORKAROUND=1 ./scripts/compile-all.sh` AND `CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh`. Every existing project must stay green with the default arg.
- [ ] **Step 5: Commit** — `feat(shared): optional rotation for CrowDisplay — portrait support + touch remap`

### Task 11: fonts + theme + GfxMeasure + ReaderView

**Files:**
- Create: `src/fonts/` — copy from `~/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/`, adapted exactly like `shared/CrowPanelShared/fonts/README.md` describes (drop the `#include <Adafruit_GFX.h>` line, add `#pragma once`): `FreeSerif{9,12,18}pt7b`, `FreeSerifBold{9,12,18,24}pt7b`, `FreeSerifItalic{9,12,18}pt7b`, `FreeSerifBoldItalic{9,12,18}pt7b`, `FreeMono{9,12,18}pt7b` — 16 files, ASCII 0x20–0x7E only (state that in TECHNICAL.md). Include them from **one** TU only (`GfxMeasure.cpp`) to avoid ODR issues; `GfxMeasure.h` exposes `const GFXfont *inkFont(uint8_t style, uint8_t fontStep)`.
- Create: `src/InkTheme.h`:

```cpp
// E-ink paper palette (24-bit; to565() at the call site like DisplayBringup).
constexpr uint32_t kInkPaper   = 0xEFE8D8;  // warm paper
constexpr uint32_t kInkText    = 0x1A1A14;  // near-black
constexpr uint32_t kInkFaint   = 0x8A8474;  // footer/progress/quote bar
constexpr uint32_t kInkCard    = 0xE4DCC8;  // library cards / HUD panels
```

Font table (the per-step mapping decided in brainstorming):

| style | step 0 | step 1 | step 2 |
|---|---|---|---|
| Body/Italic/Bold/BoldItalic | Serif 9pt variants | 12pt | 18pt |
| Mono | FreeMono 9 | 12 | 18 |
| H1 | SerifBold 18 | 24 | 24 |
| H2 | SerifBold 12 | 18 | 24 |
| H3 | SerifBold 9 | 12 | 18 |

- Create: `src/GfxMeasure.h/.cpp` — implements `Ink::TextMeasure` over `getTextBounds` on `CrowDisplay::canvas()`, with a per-(style,fontStep) advance cache for ASCII 0x20–0x7E filled lazily from the GFXfont glyph `xAdvance` fields directly (no draw calls): `textWidth` = sum of advances; `lineHeight` = font `yAdvance`. This keeps chapter pagination in the tens of milliseconds.
- Create: `src/ReaderView.h/.cpp` — draws one page from `Paginator::lines()`: set font per seg, `setCursor(marginX + indentPx + seg.x, y + baseline)`, draw quote bars (3 px `kInkFaint` rule at x=marginX for Quote lines), bullets (`•`→ a 6 px filled circle for unordered, `N.` text for ordered — number carried in `Line` via `firstOfBlock` + a small counter the view keeps), rules (centered 200 px hairline), footer (`title · ch a/b · p x/y · NN%` in Mono step-0 at pageH-40, `kInkFaint`). Page turn: optional flash — `fillScreen(text)` → `delay(90)` → normal draw (both full-screen ops; single-framebuffer tearing is a non-issue at page cadence, per spec). Tap zones handled by the .ino: x > 60 % width → next, x < 25 % → prev, else HUD (Task 12).

- [ ] **Step 1:** vendor fonts, write InkTheme + GfxMeasure; compile with `EXTRA_FLAGS="-DUSE_DISPLAY=1"`.
- [ ] **Step 2:** ReaderView + .ino wiring: `USE_DISPLAY` builds route the SAME command handlers' state into ReaderView; serial keeps working identically (one pipeline).
- [ ] **Step 3:** compile display + kitchen-sink (`-DUSE_DISPLAY=1 -DUSE_INKWELL_SD=1`) green. Verify GFX/SensorLib appear in `_arduino-build/25-inkwell/libraries/`.
- [ ] **Step 4: Commit** — `feat(25): portrait reader rendering — serif page draw, footer, e-ink flash`

### Task 12: LibraryView + HUD + TocView + AaMenu

**Files:** Create the four view files; modify `.ino` (a tiny `enum class Screen { Library, Reader, Toc } screen;` state machine + touch dispatch in `loop()`).

- LibraryView: 2-column card grid (cards 252×360, 32 px gutters, paper `kInkCard` cards, 1 px `kInkFaint` border), placeholder cover = title (Serif 12 wrapped by the real Paginator with a 220 px width — reuse, don't re-implement) + author + format badge + progress %. Tap card → open. 6 cards/screen; vertical swipe (dy > 80 px) pages the grid.
- HUD (center tap in reader): bottom sheet 600×280 on `kInkCard`: progress scrubber (tap x → `goto` permille), buttons `[Library] [Contents] [Aa] [☀-] [☀+]` (brightness = `CrowDisplay::setBacklight` ±32, clamped 32..255). Uses shared `Widgets` touch chrome (`kChrome*` names) where a widget fits; otherwise plain rects — match project 24's usage.
- TocView: full-screen list of `toc()` titles (Serif 12, 56 px rows, up to 16/page, swipe pages), tap → jump (`chapter` + `tocOffset` → `pageForOffset`).
- AaMenu: full-screen sheet with the three steppers (font step −/+, spacing 100/115/130, margins 32/48/64) + flash toggle. Every change: re-layout current chapter, re-land by saved offset, redraw — the exact `font` serial-command path.

- [ ] **Step 1:** implement + wire; every touch action must call the same handler functions the serial commands call.
- [ ] **Step 2:** compile display + kitchen-sink green.
- [ ] **Step 3: Commit** — `feat(25): library grid, HUD, contents view, Aa settings`

### Task 13: real covers (JPEGDEC + PNGdec)

**Files:**
- Create: `src/CoverArt.h/.cpp` (gated `#if USE_DISPLAY && USE_INKWELL_SD` — direct includes, **no `__has_include`**)
- Modify: `scripts/install-libs.sh`, `libraries.txt` (PNGdec — run `arduino-cli lib search PNGdec`, pin the exact version installed, same line format as the file's other entries)

- [ ] **Step 1:** `CoverArt::thumbFor(entry, uint16_t *rgb565, w=220, h=300)` — check `/books/.inkwell/<name>.thumb` (raw: `uint16 w,h` then pixels) first; else `EpubBook::coverImage` → JPEGDEC (scale via its 1/2..1/8 options to the smallest ≥ target, then nearest-neighbour to exact) or PNGdec by media-type, letterboxed on `kInkCard`; write cache; decode failure → false (LibraryView keeps the placeholder — never blocks).
- [ ] **Step 2:** kitchen-sink compile; verify `JPEGDEC`/`PNGdec` directories exist under `_arduino-build/25-inkwell/libraries/` (the `__has_include` lesson: green is not proof).
- [ ] **Step 3: Commit** — `feat(25): EPUB cover thumbnails with RGB565 SD cache`

### Task 14: gates, docs, proof state

**Files:**
- Modify: `scripts/check-flag-matrix.sh` — add `P25="in-progress/25-inkwell"` and rows:

```bash
  "$P25|baseline||"
  "$P25|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"
  "$P25|sd|-DUSE_INKWELL_SD=1|"
  "$P25|kitchen-sink|-DUSE_DISPLAY=1 -DUSE_INKWELL_SD=1|GFX Library for Arduino,SensorLib,JPEGDEC,PNGdec"
```

- Modify: `docs/full-port-proof-matrix.md` — P25 row, **compile-ready only**.
- Write real `README.md` (serial walkthrough incl. a pasted transcript of `books`→`open 3`→`next`→`toc`→`font 2`) and `TECHNICAL.md` (format subsets + degrade rules, sidecar/pos file formats, ASCII-only fonts note, rotation-unverified warning, SD `/books` layout, the PSRAM whole-epub-load decision).
- `AGENTS.md`: only if it lists per-project commands — follow whatever it does for project 24.

- [ ] **Step 1:** run ALL gates and paste outputs into the PR/commit notes:

```bash
./scripts/test-inkwell.sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

Every row green; host suite all checks passed.
- [ ] **Step 2: Commit** — `feat(25): flag-matrix rows, proof-matrix entry (compile-ready), docs`

---

## Hardware bring-up (post-plan, gated on a physical panel — do NOT mark done from a desk)

Not tasks for the implementing engineer; recorded so nobody upgrades proof rows early. Sequence per `docs/hardware-bringup-checklist.md`, one flag at a time: (1) display-only build, verify rotation quadrant + touch corners via serial log, flip `INKWELL_ROTATION` 1↔3 if mirrored; (2) +SD with a real `/books` card incl. one big EPUB; (3) covers. Each stage that passes moves the proof-matrix row with the exact FQBN + observed behavior, per the honesty contract.

## Self-review notes (already applied)

- Spec coverage: every spec section maps to a task (portrait/shared → T10; parsers → T2–4; EPUB → T5; pagination/resume/sidecars → T6/T9; covers+PNGdec → T13; flags/matrix → T9/T14; serial mock → T8; views/Aa/TOC → T11–12; error handling pinned in T4/T5 malformed tests + LibraryStore degrade rules).
- Types cross-checked: `Ink::Block/Run/Style`, `TextMeasure::textWidth(std::string, uint8_t)`, `LayoutSettings::hash()`, `TocEntry.spineIndex`, `InkBook::permille` are used with identical signatures in every task that references them.
- Known judgment call: `LineSeg.x` is assigned by the Paginator during line assembly (running x within the content box) — the renderer adds `marginX + indentPx` only.
