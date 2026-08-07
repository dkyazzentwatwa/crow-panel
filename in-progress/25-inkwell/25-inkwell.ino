// SERIAL MOCK READER. Three embedded sample books (TXT/Markdown/EPUB) page
// through the same InkBook -> Paginator pipeline the real SD-backed reader
// (Task 9) will use; only LibraryStore's bookData() source changes later.
// No display, no SD, no touch yet -- every page renders as text to Serial.

#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/InkBook.h"
#include "src/Paginator.h"
#include "src/LibraryStore.h"

SerialCommandRouter router;
EventLog eventLog;
LibraryStore library;
Ink::InkBook book;
Ink::Paginator paginator;
std::vector<Ink::Block> currentBlocks;  // current chapter's parsed blocks

// Declared before SerialMeasure so its inline method bodies (which read
// layoutSettings.fontStep) resolve against an already-declared global --
// a member function's own "complete-class" lookup defer doesn't reach
// forward into names declared later at namespace scope.
Ink::LayoutSettings layoutSettings;

// Base per-style char widths/heights are the exact host tests' MockMeasure
// tables (test/host_main.cpp), scaled by (fontStep+2)/3 -- multiplied
// before the integer divide, so the three font steps land on genuinely
// different multipliers instead of truncating to the same value:
//   fontStep 0: x2/3 (smaller)   fontStep 1: x3/3 == 1 (host-table parity,
//   the LayoutSettings default)   fontStep 2: x4/3 (larger).
// Step 1 reproducing the base tables exactly means default pagination here
// stays identical to what the host suite already proved correct; `font 1`/
// `font 3` (fontStep 0/2) are the ones that visibly re-paginate.
class SerialMeasure : public Ink::TextMeasure {
 public:
  int16_t textWidth(const std::string &s, uint8_t style) override {
    int32_t scaled = (int32_t)widths_[style] * (layoutSettings.fontStep + 2) / 3;
    return (int16_t)(s.size() * scaled);
  }
  int16_t lineHeight(uint8_t style) override {
    return (int16_t)((int32_t)heights_[style] * (layoutSettings.fontStep + 2) / 3);
  }

 private:
  int16_t widths_[Ink::kStyleCount] = {10, 11, 10, 11, 12, 20, 16, 12};
  int16_t heights_[Ink::kStyleCount] = {20, 20, 20, 20, 20, 40, 32, 24};
};

SerialMeasure measure;

int currentBookIndex = -1;  // -1 = library view, no book open
size_t currentChapter = 0;
size_t currentPage = 0;
bool bookOpen = false;

// Mirrors Paginator.cpp's own private kListIndentPerDepthPx (24px/depth) --
// there's no public accessor for it, so this is a second copy of the same
// design constant, not a derived value. Used to recover a ListItem line's
// nesting depth from Line::indentPx so nested lists are visible in the
// text-only render (Quote's indentPx is a flat 24 regardless of nesting,
// so this constant is only consulted for ListItem lines).
constexpr int16_t kListIndentPerDepthPx = 24;

const char *formatTag(Ink::Format f) {
  switch (f) {
    case Ink::Format::Txt: return "TXT";
    case Ink::Format::Markdown: return "MD";
    case Ink::Format::Epub: return "EPUB";
  }
  return "?";
}

bool requireOpen() {
  if (!bookOpen) {
    Serial.println(F("no book open -- see `books` / `open <n>`"));
    return false;
  }
  return true;
}

// Finds the block a firstOfBlock line's srcOffset belongs to, so printPage()
// can recover ListItem::ordered -- information Paginator::Line doesn't
// carry itself. currentBlocks is in ascending srcOffset order (every
// parser emits blocks in source order), and a firstOfBlock line's
// srcOffset is exactly its block's srcOffset (relOffset 0 at block start),
// so the last block whose srcOffset <= off is the exact match.
const Ink::Block *blockForOffset(uint32_t off) {
  const Ink::Block *found = nullptr;
  for (size_t i = 0; i < currentBlocks.size(); ++i) {
    if (currentBlocks[i].srcOffset <= off) {
      found = &currentBlocks[i];
    } else {
      break;
    }
  }
  return found;
}

// Wraps one seg's text in the style marker the task spec calls for. Body/
// heading styles print plain -- a heading line already gets its own
// "#"/"##"/"###" block-level prefix, and InkDoc.h guarantees every run on a
// heading line already collapsed to the heading's own style, so there is
// never a mixed-style heading line to mark up further.
String styledText(const Ink::LineSeg &seg) {
  String t(seg.text.c_str());
  switch (seg.style) {
    case Ink::kStyleBold: return "**" + t + "**";
    case Ink::kStyleItalic: return "_" + t + "_";
    case Ink::kStyleBoldItalic: return "**_" + t + "_**";
    case Ink::kStyleMono: return "`" + t + "`";
    default: return t;
  }
}

// Reconstructs a line's text, including the spaces Paginator represents as
// pixel gaps rather than literal characters between adjacent, differently
// styled segs (see Paginator.cpp's wrapWords: two same-style neighbors get
// a real ' ' merged into one seg, but a style change never inserts one --
// only an x offset). Re-measuring each seg with the same `measure` used for
// layout recovers exactly the gap the paginator intended: if the next
// seg's x lands past where this one's measured text would end, a space
// belongs between them.
String renderSegs(const Ink::Line &ln) {
  String out;
  int16_t prevEnd = 0;
  bool first = true;
  for (const Ink::LineSeg &seg : ln.segs) {
    if (!first && seg.x > prevEnd) out += ' ';
    out += styledText(seg);
    prevEnd = (int16_t)(seg.x + measure.textWidth(seg.text, seg.style));
    first = false;
  }
  return out;
}

void loadChapterAndLayout(size_t chapter) {
  currentBlocks.clear();
  book.loadChapter(chapter, currentBlocks);
  paginator.layout(currentBlocks, layoutSettings, measure);
  currentChapter = chapter;
  currentPage = 0;
}

// Saves the CURRENT page's leading offset as the resume point. Called on
// every page turn/jump/settings change, per spec -- LibraryStore holds it
// in RAM only (see LibraryStore.h): a reboot loses all progress in this
// mock backend, same as an unsaved scroll position in any app that hasn't
// hit disk yet.
void savePositionCurrent() {
  if (!bookOpen) return;
  uint32_t offset = paginator.pageStartOffset(currentPage);
  uint16_t permille = book.permille(currentChapter, offset);
  library.savePosition((size_t)currentBookIndex, (uint16_t)currentChapter, offset,
                        permille);
}

void printPage() {
  Serial.println(F("----------------------------------------------"));  // 46 dashes
  if (!bookOpen) {
    Serial.println(F("(no book open -- see `books` / `open <n>`)"));
    return;
  }

  const std::vector<Ink::Page> &pages = paginator.pages();
  const std::vector<Ink::Line> &lines = paginator.lines();
  if (pages.empty()) {
    Serial.println(F("(empty chapter)"));
  } else {
    // Clamp the MEMBER itself, not a local copy -- every other read of
    // currentPage in this function (and the footer below) sees the same
    // clamped value this way, so the body and footer can never disagree
    // about which page is actually showing.
    if (currentPage >= pages.size()) currentPage = pages.size() - 1;
    const Ink::Page &pg = pages[currentPage];
    // Ordered-list numbering restarts every page rather than tracking a
    // true continuing count across pages/lists -- a documented
    // approximation (see the task spec); a list that spans a page break
    // renumbers from 1 on the new page.
    int orderedCounter = 0;

    for (int li = pg.firstLine; li < pg.firstLine + pg.lineCount; ++li) {
      const Ink::Line &ln = lines[(size_t)li];

      if (ln.blockType == Ink::BlockType::Rule) {
        Serial.println(F("----"));
        continue;
      }

      String prefix;
      if (ln.blockType == Ink::BlockType::H1) {
        prefix = "# ";
      } else if (ln.blockType == Ink::BlockType::H2) {
        prefix = "## ";
      } else if (ln.blockType == Ink::BlockType::H3) {
        prefix = "### ";
      } else if (ln.blockType == Ink::BlockType::Quote) {
        prefix = "| ";
      } else if (ln.blockType == Ink::BlockType::ListItem) {
        // Nesting indent: 2 extra spaces per depth level beyond 1, derived
        // from Line::indentPx (24px per depth, mirrored above) -- makes
        // the sample Markdown's nested list actually visible as nesting
        // instead of every depth printing flush-left.
        int depth = ln.indentPx / kListIndentPerDepthPx;
        if (depth < 1) depth = 1;
        String indent;
        for (int i = 0; i < (depth - 1) * 2; ++i) indent += ' ';

        if (ln.firstOfBlock) {
          const Ink::Block *blk = blockForOffset(ln.srcOffset);
          if (blk != nullptr && blk->ordered) {
            ++orderedCounter;
            prefix = indent + String(orderedCounter) + ". ";
          } else {
            prefix = indent + "\xE2\x80\xA2 ";  // "bullet " (U+2022) UTF-8
          }
        } else {
          prefix = indent + "  ";  // wrapped continuation: indent only
        }
      }

      Serial.print(prefix);
      Serial.println(renderSegs(ln));
    }
  }

  const BookEntry &e = library.entry((size_t)currentBookIndex);
  String label = e.title.length() ? e.title : e.id;
  // Computed live from the CURRENT page's own start offset, not read back
  // from library.entry()'s cached permille -- that field is only as fresh
  // as the last savePositionCurrent() call, so reading it here lagged by
  // one action (e.g. `next` printed the page just left, not the one just
  // turned to, until the NEXT command ran). InkBook::permille() is cheap
  // (an O(chapterCount) prefix sum), so recomputing it per print costs
  // nothing worth caching for.
  uint16_t permille = book.permille(currentChapter, paginator.pageStartOffset(currentPage));
  Serial.print(F("-- "));
  Serial.print(label);
  Serial.print(F(" \xC2\xB7 ch "));  // " middot ch " (U+00B7)
  Serial.print(currentChapter + 1);
  Serial.print('/');
  Serial.print((unsigned)book.chapterCount());
  Serial.print(F(" \xC2\xB7 p "));
  Serial.print(currentPage + 1);
  Serial.print('/');
  Serial.print((unsigned)paginator.pageCount());
  Serial.print(F(" \xC2\xB7 "));
  Serial.print(permille / 10);
  Serial.println(F("% --"));
}

// Shared teardown back to "library view, nothing open": clears
// currentBlocks and re-layouts an empty chapter so no stale pages survive
// (paginator.layout() on an empty block list produces one blank page, per
// its own documented empty-chapter behavior) -- used by both cmdClose and
// openBook()'s failure path. InkBook::open() tears down whatever `book`
// previously held BEFORE it can fail, so a failed reopen over an
// already-open book needs exactly the same reset a deliberate `close`
// does; keeping one function means that reset can't drift between the two
// call sites.
void resetReaderState() {
  currentBlocks.clear();
  paginator.layout(currentBlocks, layoutSettings, measure);
  bookOpen = false;
  currentBookIndex = -1;
  currentChapter = 0;
  currentPage = 0;
}

void openBook(size_t idx) {
  const uint8_t *data = nullptr;
  size_t size = 0;
  if (!library.bookData(idx, data, size)) {
    Serial.println(F("[open] failed to read book data"));
    return;
  }
  const BookEntry &e = library.entry(idx);
  if (!book.open(e.format, data, size)) {
    Serial.println(F("[open] failed to parse book"));
    // Latent with today's samples (only a malformed EPUB can fail this
    // path, and sampleEpub() is always well-formed) -- real once Task 9's
    // SD store can hand back a corrupt file. See resetReaderState()'s own
    // comment for why this needs the same reset as a deliberate `close`.
    resetReaderState();
    return;
  }

  currentBookIndex = (int)idx;
  bookOpen = true;

  uint16_t spine = 0;
  uint32_t offset = 0;
  library.loadPosition(idx, spine, offset);  // false (no saved position) -> 0, 0
  if (spine >= book.chapterCount()) spine = 0;

  loadChapterAndLayout(spine);
  currentPage = paginator.pageForOffset(offset);
  printPage();
  savePositionCurrent();
}

// Shared re-land pattern for font/spacing/margin: keep the reading spot
// (the current page's own start offset), re-layout the same chapter's
// blocks under the new settings, then land on whichever page now contains
// that offset -- the same resume mechanism InkBook/Paginator's own tests
// exercise for a font-size change (testPaginatorResume).
void relayoutAndLand() {
  uint32_t offset = paginator.pageStartOffset(currentPage);
  paginator.layout(currentBlocks, layoutSettings, measure);
  currentPage = paginator.pageForOffset(offset);
  printPage();
  savePositionCurrent();
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "inkwell", eventLog.size(), &router);
  Serial.print(F("library: "));
  Serial.print((unsigned)library.count());
  Serial.println(F(" books"));
  if (bookOpen) {
    const BookEntry &e = library.entry((size_t)currentBookIndex);
    Serial.print(F("open: "));
    Serial.print(e.title.length() ? e.title : e.id);
    Serial.print(F(" -- chapter "));
    Serial.print(currentChapter + 1);
    Serial.print('/');
    Serial.print((unsigned)book.chapterCount());
    Serial.print(F(", page "));
    Serial.print(currentPage + 1);
    Serial.print('/');
    Serial.print((unsigned)paginator.pageCount());
    Serial.print(F(" ("));
    Serial.print(e.permille / 10);
    Serial.println(F("%)"));
    Serial.print(F("settings: font "));
    Serial.print(layoutSettings.fontStep + 1);
    Serial.print(F(", spacing "));
    Serial.print(layoutSettings.lineSpacingPct);
    Serial.print(F("%, margin "));
    Serial.println(layoutSettings.marginX);
  } else {
    Serial.println(F("open: (none)"));
  }
  Serial.println(F("positions: in-RAM (mock) -- lost on reboot"));
}

void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdBooks(const String &) {
  Serial.println(F("library:"));
  for (size_t i = 0; i < library.count(); ++i) {
    const BookEntry &e = library.entry(i);
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(e.title.length() ? e.title : e.id);
    Serial.print(F(" by "));
    Serial.print(e.author);
    Serial.print(F(" ["));
    Serial.print(formatTag(e.format));
    Serial.print(F("] "));
    Serial.print(e.bytes);
    Serial.print(F(" bytes, "));
    Serial.print(e.permille / 10);
    Serial.println(F("% read"));
  }
}

void cmdOpen(const String &args) {
  long n = args.toInt();
  if (n < 0 || (size_t)n >= library.count()) {
    Serial.println(F("[open] invalid index -- see `books`"));
    return;
  }
  openBook((size_t)n);
}

void cmdPage(const String &) {
  if (!requireOpen()) return;
  printPage();
}

void cmdNext(const String &) {
  if (!requireOpen()) return;
  if (currentPage + 1 < paginator.pageCount()) {
    ++currentPage;
  } else if (currentChapter + 1 < book.chapterCount()) {
    loadChapterAndLayout(currentChapter + 1);
    currentPage = 0;
  } else {
    Serial.println(F("-- end of book --"));
    return;
  }
  printPage();
  savePositionCurrent();
}

void cmdPrev(const String &) {
  if (!requireOpen()) return;
  if (currentPage > 0) {
    --currentPage;
  } else if (currentChapter > 0) {
    loadChapterAndLayout(currentChapter - 1);
    currentPage = paginator.pageCount() > 0 ? paginator.pageCount() - 1 : 0;
  } else {
    Serial.println(F("-- start of book --"));
    return;
  }
  printPage();
  savePositionCurrent();
}

// Approximation (documented per the task spec): rather than resolving an
// exact byte offset from a percentage, this walks InkBook::permille(c, 0)
// across chapters to find which chapter the target percentage falls in,
// then places the page by the SAME fraction through that chapter's page
// count -- not through its byte range (InkBook doesn't expose chapter byte
// sizes on its own). Good enough to "roughly" land in the right spot for a
// mock scrubber; a real UI slider would want a tighter, offset-accurate
// version once the display path can afford it.
void cmdGoto(const String &args) {
  if (!requireOpen()) return;
  long pct = args.toInt();
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  uint16_t target = (uint16_t)(pct * 10);

  size_t chapters = book.chapterCount();
  size_t chapter = 0;
  uint16_t startPermille = 0;
  for (size_t c = 0; c < chapters; ++c) {
    uint16_t p = book.permille(c, 0);
    if (p <= target) {
      chapter = c;
      startPermille = p;
    } else {
      break;
    }
  }
  uint16_t endPermille = (chapter + 1 < chapters) ? book.permille(chapter + 1, 0) : 1000;

  if (chapter != currentChapter) loadChapterAndLayout(chapter);

  size_t pages = paginator.pageCount();
  size_t targetPage = 0;
  if (pages > 1 && endPermille > startPermille) {
    uint32_t span = (uint32_t)(endPermille - startPermille);
    uint32_t into = (uint32_t)(target - startPermille);
    targetPage = (size_t)(((uint64_t)into * (pages - 1) + span / 2) / span);
    if (targetPage >= pages) targetPage = pages - 1;
  }
  currentPage = targetPage;
  printPage();
  savePositionCurrent();
}

void cmdToc(const String &) {
  if (!requireOpen()) return;
  const std::vector<Ink::TocEntry> &toc = book.toc();
  if (toc.empty()) {
    Serial.println(F("(no table of contents)"));
    return;
  }
  for (size_t i = 0; i < toc.size(); ++i) {
    size_t chapter = 0;
    bool ok = book.tocTarget(i, chapter);
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(toc[i].title.c_str());
    if (!ok) Serial.print(F("  (unresolved)"));
    Serial.println();
  }
}

// "chapter <n>" jumps to the n-th TOC ENTRY (as listed by `toc`), not a raw
// spine index -- it goes through InkBook::tocTarget()/tocOffset(), per the
// task spec, so a TOC entry whose href never resolved to a spine item is
// refused here rather than jumping nowhere.
void cmdChapter(const String &args) {
  if (!requireOpen()) return;
  long n = args.toInt();
  const std::vector<Ink::TocEntry> &toc = book.toc();
  if (n < 0 || (size_t)n >= toc.size()) {
    Serial.println(F("[chapter] invalid TOC index -- see `toc`"));
    return;
  }
  size_t chapter = 0;
  if (!book.tocTarget((size_t)n, chapter)) {
    Serial.println(F("[chapter] unresolved TOC target"));
    return;
  }
  uint32_t offset = book.tocOffset((size_t)n);
  if (chapter != currentChapter) loadChapterAndLayout(chapter);
  currentPage = paginator.pageForOffset(offset);
  printPage();
  savePositionCurrent();
}

void cmdFont(const String &args) {
  if (!requireOpen()) return;
  long v = args.toInt();
  if (v < 1 || v > 3) {
    Serial.println(F("[font] 1-3"));
    return;
  }
  layoutSettings.fontStep = (uint8_t)(v - 1);
  relayoutAndLand();
}

void cmdSpacing(const String &args) {
  if (!requireOpen()) return;
  long v = args.toInt();
  if (v != 100 && v != 115 && v != 130) {
    Serial.println(F("[spacing] 100 | 115 | 130"));
    return;
  }
  layoutSettings.lineSpacingPct = (uint8_t)v;
  relayoutAndLand();
}

void cmdMargin(const String &args) {
  if (!requireOpen()) return;
  long v = args.toInt();
  if (v != 32 && v != 48 && v != 64) {
    Serial.println(F("[margin] 32 | 48 | 64"));
    return;
  }
  layoutSettings.marginX = (int16_t)v;
  relayoutAndLand();
}

void cmdClose(const String &) {
  if (!requireOpen()) return;
  savePositionCurrent();
  book.close();
  resetReaderState();
  Serial.println(F("closed -- back to library (see `books`)"));
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "Inkwell -- portrait e-ink-style reader (serial mock)");
  printHardwareProfile(Serial, activeHardwareProfile());

  layoutSettings.pageW = INKWELL_PAGE_W;
  layoutSettings.pageH = INKWELL_PAGE_H;

  library.begin();
  eventLog.add("Inkwell booted");
  Serial.print(F("library: "));
  Serial.print((unsigned)library.count());
  Serial.println(F(" sample books loaded"));

  router.begin(Serial, "inkwell");
  router.on("status", "scaffold and proof status", cmdStatus, "system");
  router.on("history", "recent event history", cmdHistory, "system");
  router.on("books", "list the library", cmdBooks, "reader");
  router.on("open", "N -- open a library book by index", cmdOpen, "reader");
  router.on("page", "reprint the current page", cmdPage, "reader");
  router.on("next", "turn to the next page", cmdNext, "reader");
  router.on("prev", "turn to the previous page", cmdPrev, "reader");
  router.on("goto", "PCT (0-100) -- jump to an approximate position", cmdGoto, "reader");
  router.on("toc", "list this book's table of contents", cmdToc, "reader");
  router.on("chapter", "N -- jump to TOC entry N (see `toc`)", cmdChapter, "reader");
  router.on("font", "1-3 -- set font step and re-layout", cmdFont, "reader");
  router.on("spacing", "100|115|130 -- set line spacing and re-layout", cmdSpacing,
            "reader");
  router.on("margin", "32|48|64 -- set side margin and re-layout", cmdMargin, "reader");
  router.on("close", "save position and return to the library", cmdClose, "reader");
}

void loop() {
  router.poll();
  delay(1);
}
