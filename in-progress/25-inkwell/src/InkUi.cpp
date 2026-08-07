// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT
// HARDWARE-VERIFIED -- every rect below needs eyes on glass.
#include "InkUi.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include "GfxMeasure.h"
#include "InkTheme.h"

namespace InkUi {

namespace {

// Shared geometry, portrait 600x1024. Draw and hit-test read the SAME
// constants -- that adjacency is why the views live in one file.
constexpr int16_t kW = 600, kH = 1024;
constexpr int16_t kMargin = 32;
constexpr int16_t kHeaderH = 88;

// Library grid: 2 columns x 2 rows of 252x360 cards.
constexpr int16_t kCardW = 252, kCardH = 360, kCardGap = 32;
constexpr int16_t kGridTop = kHeaderH + 16;

// HUD sheet.
constexpr int16_t kHudTop = kH - 280;
constexpr int16_t kHudBarY = kHudTop + 48, kHudBarX0 = 48, kHudBarX1 = kW - 48;
constexpr int16_t kHudRow1 = kHudTop + 96, kHudRow2 = kHudTop + 184;
constexpr int16_t kHudBtnH = 64;

// TOC list.
constexpr int16_t kTocRowH = 56, kTocTop = kHeaderH + 8;

// Aa sheet rows.
constexpr int16_t kAaRowH = 88;
constexpr int16_t kAaRow(int i) { return kHeaderH + 24 + (int16_t)(i * (kAaRowH + 24)); }

uint16_t paper() { return InkTheme::to565(InkTheme::kPaper); }
uint16_t ink() { return InkTheme::to565(InkTheme::kText); }
uint16_t faint() { return InkTheme::to565(InkTheme::kFaint); }
uint16_t card() { return InkTheme::to565(InkTheme::kCard); }

bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void header(Arduino_GFX *g, const char *title) {
  g->setFont(InkwellGfx::inkFont(Ink::kStyleH2, 1));
  g->setTextColor(ink());
  g->setCursor(kMargin, 56);
  g->print(title);
  g->drawFastHLine(kMargin, kHeaderH - 8, kW - 2 * kMargin, faint());
}

void button(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, const char *label,
            bool active = false) {
  g->drawRect(x, y, w, kHudBtnH, faint());
  if (active) g->drawRect(x + 1, y + 1, w - 2, kHudBtnH - 2, ink());
  g->setFont(InkwellGfx::inkFont(Ink::kStyleBody, 0));
  g->setTextColor(ink());
  g->setCursor(x + 12, y + kHudBtnH / 2 + 6);
  g->print(label);
}

void cardRect(size_t slot, int16_t &x, int16_t &y) {
  x = (int16_t)(kMargin + (slot % 2) * (kCardW + kCardGap));
  y = (int16_t)(kGridTop + (slot / 2) * (kCardH + kCardGap));
}

const char *fmtTag(Ink::Format f) {
  switch (f) {
    case Ink::Format::Txt: return "TXT";
    case Ink::Format::Markdown: return "MD";
    case Ink::Format::Epub: return "EPUB";
  }
  return "?";
}

// Bottom paging zones shared by Library and TOC: two modest corner strips,
// not full-width, so a stray palm on the lower screen doesn't page.
constexpr int16_t kPageZoneW = 160, kPageZoneH = 72;
bool inPagePrevZone(int16_t x, int16_t y) {
  return inRect(x, y, 0, kH - kPageZoneH, kPageZoneW, kPageZoneH);
}
bool inPageNextZone(int16_t x, int16_t y) {
  return inRect(x, y, kW - kPageZoneW, kH - kPageZoneH, kPageZoneW, kPageZoneH);
}

}  // namespace

// ---------------------------------------------------------------- Library

void drawLibrary(Arduino_GFX *g, const LibraryStore &lib, size_t gridPage) {
  if (g == nullptr) return;
  g->fillScreen(paper());
  header(g, "Inkwell");

  size_t first = gridPage * kCardsPerPage;
  for (size_t slot = 0; slot < kCardsPerPage; ++slot) {
    size_t i = first + slot;
    if (i >= lib.count()) break;
    const BookEntry &e = lib.entry(i);
    int16_t x, y;
    cardRect(slot, x, y);
    g->fillRect(x, y, kCardW, kCardH, card());
    g->drawRect(x, y, kCardW, kCardH, faint());

    // Placeholder "cover": title in serif on the card. Task 13 swaps in
    // real EPUB cover art when available and keeps this as the fallback.
    g->setFont(InkwellGfx::inkFont(Ink::kStyleBody, 1));
    g->setTextColor(ink());
    String title = e.title.length() ? e.title : e.id;
    // Crude two-line wrap: GFX print has no wrapping for custom fonts.
    if (title.length() > 18) {
      g->setCursor(x + 16, y + 64);
      g->print(title.substring(0, 18));
      g->setCursor(x + 16, y + 96);
      g->print(title.substring(18, 36));
    } else {
      g->setCursor(x + 16, y + 64);
      g->print(title);
    }
    g->setFont(InkwellGfx::inkFont(Ink::kStyleItalic, 0));
    g->setTextColor(faint());
    g->setCursor(x + 16, y + 140);
    g->print(e.author.length() ? e.author : String("unknown"));

    g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
    g->setCursor(x + 16, y + kCardH - 24);
    String meta = String(fmtTag(e.format)) + "  " + String(e.permille / 10) + "%";
    g->print(meta);
  }

  // Page indicator + zones only when the library overflows one grid.
  if (lib.count() > kCardsPerPage) {
    size_t pages = (lib.count() + kCardsPerPage - 1) / kCardsPerPage;
    g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
    g->setTextColor(faint());
    g->setCursor(kW / 2 - 30, kH - 28);
    g->print(String((unsigned)(gridPage + 1)) + "/" + String((unsigned)pages));
    g->setCursor(24, kH - 28);
    g->print("< prev");
    g->setCursor(kW - 120, kH - 28);
    g->print("next >");
  }
}

LibraryTap libraryHitTest(int16_t x, int16_t y, const LibraryStore &lib,
                          size_t gridPage) {
  LibraryTap t;
  if (lib.count() > kCardsPerPage) {
    if (inPagePrevZone(x, y)) { t.kind = LibraryTap::PagePrev; return t; }
    if (inPageNextZone(x, y)) { t.kind = LibraryTap::PageNext; return t; }
  }
  for (size_t slot = 0; slot < kCardsPerPage; ++slot) {
    size_t i = gridPage * kCardsPerPage + slot;
    if (i >= lib.count()) break;
    int16_t cx, cy;
    cardRect(slot, cx, cy);
    if (inRect(x, y, cx, cy, kCardW, kCardH)) {
      t.kind = LibraryTap::Book;
      t.index = i;
      return t;
    }
  }
  return t;
}

// -------------------------------------------------------------------- HUD

void drawHud(Arduino_GFX *g, uint16_t permille, uint8_t backlight,
             bool flashOn) {
  if (g == nullptr) return;
  // Sheet over the current page -- no full-screen clear.
  g->fillRect(0, kHudTop, kW, kH - kHudTop, card());
  g->drawFastHLine(0, kHudTop, kW, ink());

  // Progress bar with a position notch.
  g->drawRect(kHudBarX0, kHudBarY, kHudBarX1 - kHudBarX0, 16, faint());
  int16_t fillW = (int16_t)((int32_t)(kHudBarX1 - kHudBarX0 - 4) * permille / 1000);
  g->fillRect(kHudBarX0 + 2, kHudBarY + 2, fillW, 12, ink());
  g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
  g->setTextColor(ink());
  g->setCursor(kHudBarX1 - 60, kHudBarY - 10);
  g->print(String(permille / 10) + "%");

  button(g, 48, kHudRow1, 152, "Library");
  button(g, 224, kHudRow1, 152, "Contents");
  button(g, 400, kHudRow1, 152, "Aa");
  button(g, 48, kHudRow2, 152, flashOn ? "Flash: on" : "Flash: off", flashOn);
  button(g, 224, kHudRow2, 152, "Bri -");
  button(g, 400, kHudRow2, 152, "Bri +");
  g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
  g->setTextColor(faint());
  g->setCursor(230 + 152, kHudRow2 + kHudBtnH + 20);
  g->print(String((backlight * 100) / 255) + "%");
}

HudTap hudHitTest(int16_t x, int16_t y) {
  HudTap t;
  if (y < kHudTop) { t.kind = HudTap::Outside; return t; }
  if (inRect(x, y, kHudBarX0 - 8, kHudBarY - 16, kHudBarX1 - kHudBarX0 + 16, 48)) {
    t.kind = HudTap::Scrub;
    int32_t rel = x - kHudBarX0;
    if (rel < 0) rel = 0;
    if (rel > kHudBarX1 - kHudBarX0) rel = kHudBarX1 - kHudBarX0;
    t.permille = (uint16_t)(rel * 1000 / (kHudBarX1 - kHudBarX0));
    return t;
  }
  if (inRect(x, y, 48, kHudRow1, 152, kHudBtnH)) t.kind = HudTap::Library;
  else if (inRect(x, y, 224, kHudRow1, 152, kHudBtnH)) t.kind = HudTap::Contents;
  else if (inRect(x, y, 400, kHudRow1, 152, kHudBtnH)) t.kind = HudTap::Aa;
  else if (inRect(x, y, 48, kHudRow2, 152, kHudBtnH)) t.kind = HudTap::Flash;
  else if (inRect(x, y, 224, kHudRow2, 152, kHudBtnH)) t.kind = HudTap::BriDown;
  else if (inRect(x, y, 400, kHudRow2, 152, kHudBtnH)) t.kind = HudTap::BriUp;
  return t;
}

// -------------------------------------------------------------------- TOC

void drawToc(Arduino_GFX *g, const Ink::InkBook &book, size_t tocPage) {
  if (g == nullptr) return;
  g->fillScreen(paper());
  header(g, "Contents");
  g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
  g->setTextColor(faint());
  g->setCursor(kW - 110, 56);
  g->print("[back]");

  const std::vector<Ink::TocEntry> &toc = book.toc();
  size_t first = tocPage * kTocRowsPerPage;
  for (size_t r = 0; r < kTocRowsPerPage; ++r) {
    size_t i = first + r;
    if (i >= toc.size()) break;
    int16_t y = (int16_t)(kTocTop + r * kTocRowH);
    size_t chapter = 0;
    bool resolved = book.tocTarget(i, chapter);
    g->setFont(InkwellGfx::inkFont(Ink::kStyleBody, 1));
    g->setTextColor(resolved ? ink() : faint());
    g->setCursor(kMargin + 8, y + kTocRowH - 18);
    String title(toc[i].title.c_str());
    if (title.length() > 34) title = title.substring(0, 33) + "~";
    g->print(title);
    if (!resolved) {
      g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
      g->setCursor(kW - 60, y + kTocRowH - 18);
      g->print("--");
    }
    g->drawFastHLine(kMargin, y + kTocRowH, kW - 2 * kMargin, card());
  }

  if (toc.size() > kTocRowsPerPage) {
    size_t pages = (toc.size() + kTocRowsPerPage - 1) / kTocRowsPerPage;
    g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
    g->setTextColor(faint());
    g->setCursor(kW / 2 - 30, kH - 28);
    g->print(String((unsigned)(tocPage + 1)) + "/" + String((unsigned)pages));
    g->setCursor(24, kH - 28);
    g->print("< prev");
    g->setCursor(kW - 120, kH - 28);
    g->print("next >");
  }
}

TocTap tocHitTest(int16_t x, int16_t y, const Ink::InkBook &book,
                  size_t tocPage) {
  TocTap t;
  if (y < kHeaderH) { t.kind = TocTap::Back; return t; }
  if (book.toc().size() > kTocRowsPerPage) {
    if (inPagePrevZone(x, y)) { t.kind = TocTap::PagePrev; return t; }
    if (inPageNextZone(x, y)) { t.kind = TocTap::PageNext; return t; }
  }
  if (y >= kTocTop) {
    size_t r = (size_t)((y - kTocTop) / kTocRowH);
    size_t i = tocPage * kTocRowsPerPage + r;
    if (r < kTocRowsPerPage && i < book.toc().size()) {
      t.kind = TocTap::Entry;
      t.index = i;
    }
  }
  return t;
}

// --------------------------------------------------------------------- Aa

namespace {
// Row option tables shared by drawAa and aaHitTest. 3 options per row at
// fixed x slots; row 3 is the flash toggle (single wide button).
constexpr int16_t kAaOptX[3] = {48, 224, 400};
constexpr int16_t kAaOptW = 152;
const char *kAaFontLabels[3] = {"A small", "A medium", "A large"};
constexpr int16_t kAaSpacingVals[3] = {100, 115, 130};
constexpr int16_t kAaMarginVals[3] = {32, 48, 64};
}  // namespace

void drawAa(Arduino_GFX *g, const Ink::LayoutSettings &s, bool flashOn) {
  if (g == nullptr) return;
  g->fillScreen(paper());
  header(g, "Aa");
  g->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
  g->setTextColor(faint());
  g->setCursor(kW - 110, 56);
  g->print("[back]");

  struct Row { const char *label; } rows[4] = {
      {"Font size"}, {"Line spacing"}, {"Margins"}, {"Page-turn flash"}};
  for (int r = 0; r < 4; ++r) {
    g->setFont(InkwellGfx::inkFont(Ink::kStyleItalic, 0));
    g->setTextColor(faint());
    g->setCursor(kMargin, kAaRow(r) - 6);
    g->print(rows[r].label);
  }
  for (int o = 0; o < 3; ++o) {
    button(g, kAaOptX[o], kAaRow(0), kAaOptW, kAaFontLabels[o],
           s.fontStep == (uint8_t)o);
    button(g, kAaOptX[o], kAaRow(1), kAaOptW, String(kAaSpacingVals[o]).c_str(),
           s.lineSpacingPct == (uint8_t)kAaSpacingVals[o]);
    button(g, kAaOptX[o], kAaRow(2), kAaOptW, String(kAaMarginVals[o]).c_str(),
           s.marginX == kAaMarginVals[o]);
  }
  button(g, kAaOptX[0], kAaRow(3), 328, flashOn ? "Flash on" : "Flash off",
         flashOn);
}

AaTap aaHitTest(int16_t x, int16_t y) {
  AaTap t;
  if (y < kHeaderH) { t.kind = AaTap::Back; return t; }
  for (int o = 0; o < 3; ++o) {
    if (inRect(x, y, kAaOptX[o], kAaRow(0), kAaOptW, kHudBtnH)) {
      t.kind = AaTap::Font;
      t.value = (int16_t)o;
      return t;
    }
    if (inRect(x, y, kAaOptX[o], kAaRow(1), kAaOptW, kHudBtnH)) {
      t.kind = AaTap::Spacing;
      t.value = kAaSpacingVals[o];
      return t;
    }
    if (inRect(x, y, kAaOptX[o], kAaRow(2), kAaOptW, kHudBtnH)) {
      t.kind = AaTap::Margin;
      t.value = kAaMarginVals[o];
      return t;
    }
  }
  if (inRect(x, y, kAaOptX[0], kAaRow(3), 328, kHudBtnH)) t.kind = AaTap::Flash;
  return t;
}

}  // namespace InkUi

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
