// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT
// HARDWARE-VERIFIED -- baseline placement and the flash timing need eyes
// on a real panel (see TECHNICAL.md bring-up notes).
#include "ReaderView.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino.h>

#include "GfxMeasure.h"
#include "InkTheme.h"

namespace ReaderView {

namespace {

// Mirrors the Paginator's private indent constant (Paginator.cpp
// kListIndentPerDepthPx); ReaderView derives list depth from
// Line::indentPx exactly like the serial renderer does.
constexpr int16_t kListIndentPerDepthPx = 24;

// GFX custom fonts draw from the BASELINE, not the top-left. Place the
// baseline at ~3/4 of the line box: FreeFonts carry roughly 75/25
// ascent/descent in yAdvance. Good enough for v1; verify on glass.
int16_t baselineFor(int16_t lineTop, int16_t lineHeight) {
  return lineTop + (int16_t)((int32_t)lineHeight * 3 / 4);
}

// Recover the source block for a line (ordered-list numbering), same
// exact-match-by-srcOffset recovery the serial renderer uses.
const Ink::Block *blockForOffset(const std::vector<Ink::Block> &blocks,
                                 uint32_t off) {
  const Ink::Block *best = nullptr;
  for (const Ink::Block &b : blocks) {
    if (b.srcOffset <= off) best = &b;
    else break;
  }
  return best;
}

}  // namespace

void drawPage(Arduino_GFX *gfx, const Ink::Paginator &p, size_t pageIdx,
              const std::vector<Ink::Block> &blocks,
              const Ink::LayoutSettings &settings, uint8_t fontStep,
              const FooterInfo &footer, bool invertFlash) {
  if (!gfx || pageIdx >= p.pageCount()) return;
  const uint16_t paper = InkTheme::to565(InkTheme::kPaper);
  const uint16_t ink = InkTheme::to565(InkTheme::kText);
  const uint16_t faint = InkTheme::to565(InkTheme::kFaint);

  (void)invertFlash;  // the flash frame is the .ino's job: under manual
                      // flush an intermediate fillScreen here never reaches
                      // the panel (no sync between fill and delay).
  // GFX text-size multiplier is STICKY -- DisplayBringup's boot status
  // screen leaves it at 3-4, which rendered every custom-font glyph at 3-4x
  // and made pages overlap into garbage on first hardware boot. Reset it
  // before any text.
  gfx->setTextSize(1);
  gfx->fillScreen(paper);

  const Ink::Page &page = p.pages()[pageIdx];
  int16_t y = settings.marginTop;
  int orderedCounter = 0;
  for (int li = 0; li < page.lineCount; ++li) {
    const Ink::Line &ln = p.lines()[page.firstLine + li];
    int16_t baseX = settings.marginX + ln.indentPx;

    if (ln.blockType == Ink::BlockType::Rule) {
      // Centered 200px hairline, vertically centered in the line box.
      int16_t rx = (int16_t)((gfx->width() - 200) / 2);
      gfx->drawFastHLine(rx, y + ln.height / 2, 200, faint);
    } else {
      if (ln.firstOfBlock && ln.blockType == Ink::BlockType::ListItem) {
        const Ink::Block *b = blockForOffset(blocks, ln.srcOffset);
        bool ordered = b && b->ordered;
        if (ordered) ++orderedCounter;
        else orderedCounter = 0;
        int16_t markX = baseX - 16;
        if (ordered) {
          // Numbering restarts each page, same documented approximation
          // as the serial renderer.
          gfx->setFont(InkwellGfx::inkFont(Ink::kStyleBody, fontStep));
          gfx->setTextColor(ink);
          gfx->setCursor(markX - 8, baselineFor(y, ln.height));
          gfx->print(orderedCounter);
          gfx->print('.');
        } else {
          gfx->fillCircle(markX, y + ln.height / 2, 3, ink);
        }
      }
      if (ln.blockType == Ink::BlockType::Quote) {
        // 3px quote bar in the gutter the indent opened up.
        gfx->fillRect(settings.marginX + 8, y, 3, ln.height, faint);
      }
      for (const Ink::LineSeg &seg : ln.segs) {
        gfx->setFont(InkwellGfx::inkFont(seg.style, fontStep));
        gfx->setTextColor(ink);
        gfx->setCursor(baseX + seg.x, baselineFor(y, ln.height));
        gfx->print(seg.text.c_str());
      }
    }
    y = (int16_t)(y + ln.height);
  }

  // Footer: small mono line at the bottom margin, faint.
  gfx->setFont(InkwellGfx::inkFont(Ink::kStyleMono, 0));
  gfx->setTextColor(faint);
  gfx->setCursor(settings.marginX, gfx->height() - 20);
  String f = String(footer.title);
  if (f.length() > 24) f = f.substring(0, 23) + "~";
  f += " · ch " + String((unsigned)(footer.chapter + 1)) + "/" +
       String((unsigned)footer.chapterCount) + " · p " +
       String((unsigned)(footer.page + 1)) + "/" +
       String((unsigned)footer.pageCount) + " · " +
       String(footer.permille / 10) + "%";
  gfx->print(f);
}

}  // namespace ReaderView

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
