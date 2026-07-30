#include "PokedexDashboard.h"

#include <CrowPanelShared.h>
#include "PokedexSprites.h"
#include "PokedexText.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

namespace {

// Grid browse geometry. The rail takes a fixed strip on the left; the remaining
// width divides into 6 columns and 3 rows of 18 tiles total. Kept outside the
// USE_DISPLAY-gated section below because showGrid() needs kDexBuckets to
// compute railCursor_ even when called from a non-display build.
constexpr int16_t kRailX = 24;
constexpr int16_t kRailY = 104;
constexpr int16_t kRailW = 40;
constexpr int16_t kRailH = 420;
constexpr uint8_t kRailSlots = 26;  // A-Z, or 11 dex buckets using the first 11.
constexpr uint8_t kDexBuckets = 11;  // 0,100,...,1000

constexpr int16_t kGridX = 76;
constexpr int16_t kGridY = 104;
constexpr int16_t kGridW = 924;
constexpr int16_t kGridH = 420;
constexpr uint8_t kGridCols = 6;
constexpr uint8_t kGridRows = 3;
constexpr int16_t kGridCellW = kGridW / kGridCols;   // 154
constexpr int16_t kGridCellH = kGridH / kGridRows;   // 140

static_assert(kGridCols * kGridRows == POKEDEX_MAX_RESULTS,
              "grid must hold exactly one result page");

}  // namespace

void PokedexDashboard::begin() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::begin(activeHardwareProfile(), "POKEDEX PANEL");
#endif
  dirty_ = true;
}

void PokedexDashboard::showList(const PokedexRow *rows, uint8_t count, uint8_t selected,
                                uint16_t browseStart, uint16_t totalRows,
                                const String &query, const String &source,
                                const String &status) {
  // Search results are a one-off set, not a catalog window: offset 0, and leave
  // the rail disabled so nothing suggests a position in the catalog.
  (void)browseStart;
  query_ = query;
  pokedex::Filter neutral;
  neutral.showShadows = true;
  showGrid(rows, count, selected, 0, totalRows, order_, neutral, nullptr, source, status);
}

void PokedexDashboard::showDetail(const PokedexDetail &detail, uint8_t page, const String &source,
                                  const String &status) {
  detail_ = detail;
  detailPage_ = page % POKEDEX_DETAIL_PAGE_COUNT;
  source_ = source;
  status_ = status;
  mode_ = kPokedexUiDetail;
  dirty_ = true;
}

void PokedexDashboard::showGrid(const PokedexRow *rows, uint8_t count, uint8_t selected,
                                uint32_t startOrdinal, uint32_t totalMatching,
                                pokedex::Order order, const pokedex::Filter &filter,
                                const bool *railEnabled, const String &source,
                                const String &status) {
  rowCount_ = min(count, (uint8_t)POKEDEX_MAX_RESULTS);
  for (uint8_t i = 0; i < rowCount_; i++) rows_[i] = rows[i];
  selected_ = (rowCount_ == 0) ? 0 : min(selected, (uint8_t)(rowCount_ - 1));
  startOrdinal_ = startOrdinal;
  totalMatching_ = totalMatching;
  order_ = order;
  filter_ = filter;
  for (uint8_t i = 0; i < kRailSlotsMax; i++) {
    railEnabled_[i] = railEnabled != nullptr && railEnabled[i];
  }
  source_ = source;
  status_ = status;
  mode_ = kPokedexUiList;
  dirty_ = true;

  railCursor_ = 0;
  if (rowCount_ > 0) {
    if (order_ == pokedex::kOrderName) {
      const char first = rows_[0].name[0];
      const char upper = (first >= 'a' && first <= 'z') ? (char)(first - 'a' + 'A') : first;
      if (upper >= 'A' && upper <= 'Z') railCursor_ = (uint8_t)(upper - 'A');
    } else {
      const uint16_t dex = (uint16_t)atoi(rows_[0].dex);
      const uint8_t bucket = (uint8_t)(dex / 100);
      railCursor_ = bucket < kDexBuckets ? bucket : (uint8_t)(kDexBuckets - 1);
    }
  }
}

void PokedexDashboard::beginSearch(const String &initial) {
  searchInput_ = initial;
  searchCursor_ = searchInput_.length();
  keyboard_.reset();
  mode_ = kPokedexUiSearch;
  dirty_ = true;
}

bool PokedexDashboard::tick(PokedexUiEvent &event) {
  event = PokedexUiEvent();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::tick();
  if (CrowDisplay::canvas() == nullptr) {
    return false;
  }
  if (dirty_) {
    draw();
    dirty_ = false;
  }

  int16_t tx;
  int16_t ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  bool tapped = touched && !wasTouched_;
  wasTouched_ = touched;
  if (!tapped) {
    return false;
  }
  return handleTouch(tx, ty, event);
#else
  return false;
#endif
}

void PokedexDashboard::requestRepaint() {
  dirty_ = true;
}

void PokedexDashboard::printTouchDiagnostics(Print &out) const {
  out.print(F("[touch] count="));
  out.print(touchCount_);
  out.print(F(" raw="));
  out.print(lastRawX_);
  out.print(F(","));
  out.print(lastRawY_);
  out.print(F(" mapped="));
  out.print(lastTouchX_);
  out.print(F(","));
  out.print(lastTouchY_);
  out.print(F(" cfg minX="));
  out.print(POKEDEX_TOUCH_MIN_X);
  out.print(F(" maxX="));
  out.print(POKEDEX_TOUCH_MAX_X);
  out.print(F(" minY="));
  out.print(POKEDEX_TOUCH_MIN_Y);
  out.print(F(" maxY="));
  out.print(POKEDEX_TOUCH_MAX_Y);
  out.print(F(" swap="));
  out.print(POKEDEX_TOUCH_SWAP_XY);
  out.print(F(" invX="));
  out.print(POKEDEX_TOUCH_INVERT_X);
  out.print(F(" invY="));
  out.print(POKEDEX_TOUCH_INVERT_Y);
  out.print(F(" auto="));
  out.println(POKEDEX_TOUCH_AUTO_REMAP);
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {

constexpr int16_t kScreenW = 1024;
constexpr int16_t kScreenH = 600;
constexpr int16_t kHeaderH = 84;
constexpr int16_t kFooterY = 544;
constexpr int16_t kListX = 24;
constexpr int16_t kListY = 104;
constexpr int16_t kListW = 332;
constexpr int16_t kListH = 420;
constexpr int16_t kHeroX = 380;
constexpr int16_t kHeroY = 104;
constexpr int16_t kHeroW = 360;
constexpr int16_t kHeroH = 420;
constexpr int16_t kSideX = 764;
constexpr int16_t kSideY = 104;
constexpr int16_t kSideW = 236;
constexpr int16_t kSideH = 420;
constexpr int16_t kRowH = 46;

constexpr uint16_t kDexRed = Widgets::rgb(0xD8, 0x28, 0x34);
constexpr uint16_t kDexRedDark = Widgets::rgb(0x75, 0x15, 0x21);
constexpr uint16_t kBlue = Widgets::rgb(0x2B, 0x8D, 0xFF);
constexpr uint16_t kGold = Widgets::rgb(0xFF, 0xCB, 0x45);
constexpr uint16_t kWhite = Widgets::rgb(0xF8, 0xFA, 0xFC);
constexpr uint16_t kInk = Widgets::rgb(0x08, 0x0D, 0x16);
constexpr uint16_t kCard = Widgets::rgb(0x12, 0x19, 0x24);
constexpr uint16_t kCardHi = Widgets::rgb(0x1B, 0x26, 0x35);
constexpr uint16_t kMuted = Widgets::rgb(0x91, 0xA3, 0xB8);
constexpr uint16_t kLine = Widgets::rgb(0x2B, 0x3A, 0x4B);

bool inside(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int16_t clampCoord(long value, int16_t maxValue) {
  if (value < 0) return 0;
  if (value > maxValue) return maxValue;
  return (int16_t)value;
}

int16_t mapAxis(int16_t value, int16_t inMin, int16_t inMax, int16_t outMax) {
  if (inMax == inMin) return 0;
  long mapped = (long)(value - inMin) * outMax / (long)(inMax - inMin);
  return clampCoord(mapped, outMax);
}

String fitText(Arduino_GFX *g, String text, int16_t maxW, const uint8_t *font) {
  text.trim();
  if (PokedexText::width(g, text.c_str(), font) <= maxW) {
    return text;
  }
  while (text.length() > 3 && PokedexText::width(g, (text + "...").c_str(), font) > maxW) {
    text.remove(text.length() - 1);
  }
  text += "...";
  return text;
}

void label(Arduino_GFX *g, int16_t x, int16_t y, const char *text) {
  PokedexText::draw(g, x, y, text, PokedexText::fontS(), kMuted, PokedexText::kLeft);
}

uint16_t typeColor(const char *type) {
  String t(type ? type : "");
  t.toLowerCase();
  if (t == "fire") return Widgets::rgb(0xF9, 0x73, 0x16);
  if (t == "water") return Widgets::rgb(0x38, 0x82, 0xF6);
  if (t == "grass") return Widgets::rgb(0x22, 0xC5, 0x5E);
  if (t == "electric") return kGold;
  if (t == "psychic") return Widgets::rgb(0xEC, 0x48, 0x99);
  if (t == "ghost") return Widgets::rgb(0x8B, 0x5C, 0xF6);
  if (t == "dragon") return Widgets::rgb(0x06, 0xB6, 0xD4);
  if (t == "fighting") return Widgets::rgb(0xEF, 0x44, 0x44);
  if (t == "steel") return Widgets::rgb(0x94, 0xA3, 0xB8);
  if (t == "poison") return Widgets::rgb(0xA8, 0x55, 0xF7);
  if (t == "flying") return Widgets::rgb(0x7D, 0xB9, 0xFF);
  if (t == "normal") return Widgets::rgb(0xD4, 0xD4, 0xD8);
  if (t == "ice") return Widgets::rgb(0x67, 0xE8, 0xF9);
  if (t == "rock" || t == "ground") return Widgets::rgb(0xD9, 0xA4, 0x41);
  if (t == "dark") return Widgets::rgb(0x64, 0x54, 0x52);
  if (t == "fairy") return Widgets::rgb(0xF0, 0xAB, 0xFC);
  if (t == "bug") return Widgets::rgb(0x84, 0xCC, 0x16);
  return Widgets::kAccent;
}

void drawButton(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, const char *text,
                uint16_t fill = kCardHi) {
  Widgets::panel(g, x, y, w, 38, 8, fill, 1, kLine);
  PokedexText::draw(g, x + w / 2, y + 9, text, PokedexText::fontS(), kWhite,
                    PokedexText::kCenter);
}

void drawTypeChip(Arduino_GFX *g, int16_t x, int16_t y, const char *type) {
  if (type == nullptr || type[0] == '\0') return;
  int16_t textW = PokedexText::width(g, type, PokedexText::fontS());
  int16_t w = textW + 28;
  Widgets::panel(g, x, y, w, 34, 17, typeColor(type));
  PokedexText::draw(g, x + w / 2, y + 8, type, PokedexText::fontS(), kInk,
                    PokedexText::kCenter);
}

void drawWrapped(Arduino_GFX *g, String text, int16_t x, int16_t y, int16_t w, int16_t lineH,
                 uint8_t maxLines, const uint8_t *font, uint16_t color) {
  text.trim();
  if (text.length() == 0) text = "-";
  for (uint8_t line = 0; line < maxLines && text.length() > 0; line++) {
    String chunk = text;
    while (chunk.length() > 0 && PokedexText::width(g, chunk.c_str(), font) > w) {
      int split = chunk.lastIndexOf(' ');
      if (split <= 0) {
        chunk.remove(chunk.length() - 1);
      } else {
        chunk = chunk.substring(0, split);
      }
    }
    if (chunk.length() == 0) {
      chunk = text.substring(0, min((int)text.length(), 12));
    }
    bool truncated = chunk.length() < text.length() && line + 1 == maxLines;
    if (truncated && chunk.length() > 3) {
      chunk.remove(chunk.length() - 3);
      chunk += "...";
    }
    PokedexText::draw(g, x, y + line * lineH, chunk.c_str(), font, color, PokedexText::kLeft);
    text.remove(0, chunk.length());
    text.trim();
  }
}

void drawPokeball(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t r, uint16_t topColor) {
  uint16_t shadow = Widgets::rgb(0x05, 0x08, 0x10);
  uint16_t ring = Widgets::rgb(0xE5, 0xE7, 0xEB);
  g->fillCircle(cx + 8, cy + 10, r + 8, shadow);
  g->fillCircle(cx, cy, r + 8, kInk);
  g->fillCircle(cx, cy, r + 2, ring);
  g->fillCircle(cx, cy, r - 4, kWhite);

  // Draw only the top half in the type color. The previous full-circle +
  // rectangle composition looked muddy on the panel because the middle stripe
  // and lower white half fought the anti-aliased circle edge.
  for (int16_t dy = -r + 6; dy <= -12; dy++) {
    int16_t half = (int16_t)sqrt((float)(r - 6) * (r - 6) - (float)dy * dy);
    g->drawFastHLine(cx - half, cy + dy, half * 2, topColor);
  }

  g->fillRoundRect(cx - r + 12, cy - 12, (r - 12) * 2, 24, 12, kInk);
  g->fillCircle(cx, cy, 34, kInk);
  g->fillCircle(cx, cy, 22, ring);
  g->fillCircle(cx, cy, 12, Widgets::rgb(0x9C, 0xA3, 0xAF));
  g->drawCircle(cx, cy, r + 2, kWhite);
  g->drawCircle(cx, cy, r + 7, kInk);
  g->drawLine(cx - r + 28, cy - r + 28, cx - r + 58, cy - r + 12, kWhite);
  g->drawLine(cx - r + 32, cy - r + 39, cx - r + 72, cy - r + 18, kWhite);
}

void statBar(Arduino_GFX *g, int16_t x, int16_t y, const char *labelText, uint16_t value,
             uint16_t color) {
  label(g, x, y, labelText);
  char buf[12];
  snprintf(buf, sizeof(buf), "%u", value);
  PokedexText::draw(g, x + 74, y - 2, buf, PokedexText::fontS(), kWhite, PokedexText::kRight);
  Widgets::hBar(g, x + 84, y + 4, 190, 12, min(1.0f, value / 350.0f), color,
                Widgets::rgb(0x20, 0x2B, 0x38));
}

}  // namespace

void PokedexDashboard::drawHeader(Arduino_GFX *g, const char *title) {
  g->fillRect(0, 0, kScreenW, kHeaderH, kDexRed);
  g->fillRect(0, kHeaderH - 8, kScreenW, 8, kDexRedDark);
  g->fillCircle(48, 42, 25, kWhite);
  g->fillCircle(48, 42, 17, kBlue);
  g->fillCircle(48, 42, 8, Widgets::rgb(0xA7, 0xF3, 0xD0));
  PokedexText::draw(g, 92, 18, title, PokedexText::fontL(), kWhite, PokedexText::kLeft);
  PokedexText::draw(g, 92, 51, source_.c_str(), PokedexText::fontS(),
                    Widgets::rgb(0xFE, 0xF3, 0xC7), PokedexText::kLeft);
  String headerStatus = fitText(g, status_, 190, PokedexText::fontS());
  int16_t statusW = PokedexText::width(g, headerStatus.c_str(), PokedexText::fontS()) + 24;
  int16_t statusX = kScreenW - 24 - statusW;
  bool sdOk = status_.indexOf("SD catalog") >= 0;
  bool sdError = status_.indexOf("FAILED") >= 0 || status_.indexOf("MISSING") >= 0 ||
                 status_.indexOf("INVALID") >= 0;
  Widgets::panel(g, statusX, 27, statusW, 30, 15,
                 sdOk ? Widgets::kGreen : (sdError ? kDexRedDark : kGold));
  PokedexText::draw(g, statusX + statusW / 2, 34, headerStatus.c_str(), PokedexText::fontS(),
                    kInk, PokedexText::kCenter);
}

void PokedexDashboard::drawFooter(Arduino_GFX *g) {
  g->fillRect(0, kFooterY - 12, kScreenW, kScreenH - kFooterY + 12, Widgets::kBg);
  if (mode_ == kPokedexUiList) {
    drawButton(g, 24, kFooterY, 96, "TOP");
    drawButton(g, 128, kFooterY, 104, "PREV");
    drawButton(g, 240, kFooterY, 104, "NEXT");
    drawButton(g, 352, kFooterY, 118, "SEARCH", kDexRedDark);
    drawButton(g, 478, kFooterY, 104,
               order_ == pokedex::kOrderName ? "A-Z" : "DEX");
    drawButton(g, 590, kFooterY, 130,
               filter_.type1 == pokedex::kTypeAny
                   ? "TYPE: ALL"
                   : pokedex::typeNameFromId(filter_.type1));
    drawButton(g, 728, kFooterY, 140,
               filter_.showShadows ? "SHADOWS ON" : "SHADOWS OFF");
    // A range, not a page number: the window is offset-based and need not be
    // page-aligned, so "Page n/m" would be meaningless after a rail jump.
    char range[32];
    if (rowCount_ == 0) {
      snprintf(range, sizeof(range), "0 of %lu", (unsigned long)totalMatching_);
    } else {
      snprintf(range, sizeof(range), "%lu-%lu of %lu",
               (unsigned long)(startOrdinal_ + 1),
               (unsigned long)(startOrdinal_ + rowCount_),
               (unsigned long)totalMatching_);
    }
    PokedexText::draw(g, 1000, kFooterY + 8, range, PokedexText::fontS(), kMuted,
                      PokedexText::kRight);
  } else if (mode_ == kPokedexUiDetail) {
    drawButton(g, 24, kFooterY, 112, "LIST");
    static const char *const kTabs[POKEDEX_DETAIL_PAGE_COUNT] = {
        "ENTRY", "STATS", "MOVES", "MATCHUPS", "EVO"};
    for (uint8_t i = 0; i < POKEDEX_DETAIL_PAGE_COUNT; i++) {
      const int16_t x = 168 + i * 168;
      drawButton(g, x, kFooterY, 158, kTabs[i],
                 i == detailPage_ ? kDexRedDark : kCardHi);
    }
  }
}

void PokedexDashboard::drawRail(Arduino_GFX *g) {
  Widgets::panel(g, kRailX, kRailY, kRailW, kRailH, 6, kCard, 1, kLine);
  const uint8_t slots = (order_ == pokedex::kOrderName) ? kRailSlots : kDexBuckets;
  const int16_t slotH = kRailH / slots;

  for (uint8_t i = 0; i < slots; i++) {
    const int16_t y = kRailY + i * slotH;
    char label[6];
    if (order_ == pokedex::kOrderName) {
      label[0] = (char)('A' + i);
      label[1] = '\0';
    } else {
      snprintf(label, sizeof(label), "%u", (unsigned)(i * 100));
    }
    // A letter or bucket with no rows under the active filter is dimmed and
    // ignored by the touch handler (Task 14).
    const bool enabled = railEnabled_[i];
    const uint16_t colour = enabled ? kWhite : kLine;
    if (enabled && i == railCursor_) {
      Widgets::panel(g, kRailX + 3, y + 1, kRailW - 6, slotH - 2, 4, kCardHi, 0, kCardHi);
    }
    PokedexText::draw(g, kRailX + kRailW / 2, y + (slotH - 12) / 2, label,
                      PokedexText::fontS(), enabled && i == railCursor_ ? kGold : colour,
                      PokedexText::kCenter);
  }
}

void PokedexDashboard::drawTouchDot(Arduino_GFX *g) {
  if (!showTouchDot_) return;
  g->fillCircle(lastTouchX_, lastTouchY_, 11, kInk);
  g->drawCircle(lastTouchX_, lastTouchY_, 11, kGold);
  g->drawLine(lastTouchX_ - 15, lastTouchY_, lastTouchX_ + 15, lastTouchY_, kGold);
  g->drawLine(lastTouchX_, lastTouchY_ - 15, lastTouchX_, lastTouchY_ + 15, kGold);
}

void PokedexDashboard::drawTile(Arduino_GFX *g, uint8_t slot, const PokedexRow &row,
                                bool active) {
  const int16_t col = slot % kGridCols;
  const int16_t rowIndex = slot / kGridCols;
  const int16_t x = kGridX + col * kGridCellW;
  const int16_t y = kGridY + rowIndex * kGridCellH;
  const uint16_t fill = active ? Widgets::rgb(0x27, 0x38, 0x4C) : Widgets::rgb(0x10, 0x17, 0x21);
  const uint16_t border = active ? typeColor(row.type1) : kLine;

  Widgets::panel(g, x + 4, y + 4, kGridCellW - 8, kGridCellH - 8, 6, fill,
                 active ? 2 : 1, border);

  // Sprite, centred. Sprite BMPs have no alpha, so the well MUST be filled with
  // literal RGB565 0x0000 (not kInk or any other "black-ish" colour) and the
  // blit keys out the sprite's own sampled background colour.
  //
  // Why the well has to be exactly 0x0000: a black-background sprite's interior
  // outline/eye pixels are ALSO literal 0x0000 (1318 of 1573 sprites have such
  // pixels), and the transparent-colour blit cannot distinguish "background
  // black" from "outline black" - both get skipped identically. That only reads
  // correctly if the pixels underneath are the same 0x0000, so the skipped
  // outline pixels still show as black rather than as whatever tile colour was
  // there. Any other well colour would punch the outlines out of those sprites.
  const int16_t artCx = x + kGridCellW / 2;
  const int16_t artCy = y + 12 + 40;
  uint16_t key = 0;
  const uint16_t *pixels =
      (sprites_ != nullptr) ? sprites_->tile(row.entryId, &key) : nullptr;
  if (pixels != nullptr) {
    const int16_t size = sprites_->tileSize();
    g->fillRect(artCx - size / 2, y + 12, size, size, 0x0000);
    g->draw16bitRGBBitmapWithTranColor(artCx - size / 2, y + 12, (uint16_t *)pixels,
                                       key, size, size);
  } else {
    drawPokeball(g, artCx, artCy, 56, typeColor(row.type1));
  }

  String name = fitText(g, row.name, kGridCellW - 20, PokedexText::fontS());
  PokedexText::draw(g, artCx, y + kGridCellH - 44, name.c_str(), PokedexText::fontS(),
                    active ? kWhite : kMuted, PokedexText::kCenter);
  char dex[12];
  snprintf(dex, sizeof(dex), "#%s", row.dex);
  PokedexText::draw(g, artCx, y + kGridCellH - 24, dex, PokedexText::fontS(),
                    typeColor(row.type1), PokedexText::kCenter);
}

void PokedexDashboard::drawGrid(Arduino_GFX *g) {
  drawHeader(g, "POKEDEX");
  drawRail(g);

  if (rowCount_ == 0) {
    Widgets::panel(g, kGridX, kGridY, kGridW, kGridH, 8, kCard, 1, kLine);
    PokedexText::draw(g, kGridX + kGridW / 2, kGridY + 180, "No matches",
                      PokedexText::fontL(), kWhite, PokedexText::kCenter);
    PokedexText::draw(g, kGridX + kGridW / 2, kGridY + 226,
                      "Clear the type filter or enable shadows.", PokedexText::fontS(),
                      kMuted, PokedexText::kCenter);
  } else {
    for (uint8_t i = 0; i < rowCount_; i++) {
      drawTile(g, i, rows_[i], i == selected_);
    }
  }
  drawFooter(g);
  drawTouchDot(g);
}

void PokedexDashboard::drawDetail(Arduino_GFX *g) {
  drawHeader(g, "POKEDEX DETAIL");
  uint16_t primary = typeColor(detail_.type1);

  Widgets::panel(g, kListX, kListY, kListW, kListH, 8, kCard, 1, kLine);
  PokedexText::draw(g, kListX + 24, kListY + 24, "ENTRY", PokedexText::fontS(), kGold,
                    PokedexText::kLeft);
  char title[96];
  snprintf(title, sizeof(title), "#%s %s", detail_.dex, detail_.name);
  drawWrapped(g, title, kListX + 24, kListY + 72, kListW - 48, 36, 2, PokedexText::fontL(), kWhite);
  drawTypeChip(g, kListX + 24, kListY + 156, detail_.type1);
  drawTypeChip(g, kListX + 148, kListY + 156, detail_.type2);
  label(g, kListX + 24, kListY + 214, "TAGS");
  drawWrapped(g, detail_.tags, kListX + 24, kListY + 246, kListW - 48, 24, 3, PokedexText::fontS(),
              kMuted);
  label(g, kListX + 24, kListY + 344, "SOURCE");
  drawWrapped(g, detail_.sourceFile, kListX + 24, kListY + 374, kListW - 48, 24, 2,
              PokedexText::fontS(), kMuted);

  Widgets::panel(g, kHeroX, kHeroY, kHeroW, kHeroH, 8, kCard, 1, kLine);
  // Same colour-keying rationale as drawTile in the grid (Task 13): the well
  // must be literal 0x0000, not any other "black-ish" colour, because a
  // black-background sprite's interior outline pixels are also 0x0000 and are
  // keyed identically to the background - only correct if what's underneath is
  // the same 0x0000.
  //
  // Vertical budget: the ATK/DEF/HP stat bars below are drawn at fixed
  // offsets kHeroY+288/328/368 (unchanged), so the sprite has ~280px above
  // that to work with. draw16bitRGBBitmapWithTranColor cannot rescale a
  // bitmap - it blits the source's w*h pixels 1:1 (see Arduino_GFX.cpp) - so
  // rather than crop a larger cached bitmap to fit (measured against the real
  // pack: 315 of 1573 sprites have art reaching into what would be the
  // cropped-off bottom rows, several all the way to the last row, which
  // visibly chops off feet/tails), POKEDEX_SPRITE_HERO_SCALE is 7 (280px),
  // sized to fit this budget without cropping anything.
  constexpr int16_t kHeroSpriteTop = 6;
  uint16_t heroKey = 0;
  const uint16_t *heroPixels =
      (sprites_ != nullptr) ? sprites_->hero(detail_.entryId, &heroKey) : nullptr;
  if (heroPixels != nullptr) {
    const int16_t size = sprites_->heroSize();
    const int16_t hx = kHeroX + (kHeroW - size) / 2;
    const int16_t hy = kHeroY + kHeroSpriteTop;
    g->fillRect(hx, hy, size, size, 0x0000);
    g->draw16bitRGBBitmapWithTranColor(hx, hy, (uint16_t *)heroPixels, heroKey, size,
                                       size);
  } else {
    drawPokeball(g, kHeroX + kHeroW / 2, kHeroY + kHeroSpriteTop + 140, 110, primary);
  }
  statBar(g, kHeroX + 42, kHeroY + 288, "ATK", detail_.atk, kDexRed);
  statBar(g, kHeroX + 42, kHeroY + 328, "DEF", detail_.def, kBlue);
  statBar(g, kHeroX + 42, kHeroY + 368, "HP", detail_.hp, Widgets::kGreen);
  char buddy[64];
  snprintf(buddy, sizeof(buddy), "Buddy %ukm   2nd move %lu dust", detail_.buddyKm,
           (unsigned long)detail_.secondMoveStardust);
  PokedexText::draw(g, kHeroX + kHeroW / 2, kHeroY + 402, buddy, PokedexText::fontS(), kMuted,
                    PokedexText::kCenter);

  Widgets::panel(g, kSideX, kSideY, kSideW, kSideH, 8, kCard, 1, kLine);
  const char *section = "MATCHUP";
  String body;
  if (detailPage_ == 0) {
    section = "TRAINER NOTE";
    body = detail_.trainerNote;
  } else if (detailPage_ == 1) {
    section = "WEAKNESSES";
    body = detail_.weaknesses;
  } else if (detailPage_ == 2) {
    section = "RESISTANCES";
    body = detail_.resistances;
  } else if (detailPage_ == 3) {
    section = "EVOLUTION";
    body = detail_.evolution;
  } else {
    section = "MOVES";
    body = String("Fast: ") + detail_.fastMoves + "\nCharged: " + detail_.chargedMoves;
  }
  PokedexText::draw(g, kSideX + 20, kSideY + 20, section, PokedexText::fontS(), kGold,
                    PokedexText::kLeft);
  drawWrapped(g, body, kSideX + 20, kSideY + 72, kSideW - 40, 28, 10, PokedexText::fontS(), kWhite);
  drawFooter(g);
  drawTouchDot(g);
}

void PokedexDashboard::drawSearch(Arduino_GFX *g) {
  drawHeader(g, "SEARCH CATALOG");
  Widgets::panel(g, 42, 96, 940, 178, 14, kCard, 1, kLine);
  PokedexText::draw(g, 72, 116, "QUERY", PokedexText::fontS(), kGold, PokedexText::kLeft);
  String shown = searchInput_.length() ? searchInput_ : "Type a name, dex number, or type";
  uint16_t color = searchInput_.length() ? kWhite : kMuted;
  String fitted = fitText(g, shown, 860, PokedexText::fontXL());
  PokedexText::draw(g, 72, 164, fitted, PokedexText::fontXL(), color, PokedexText::kLeft);
  if (searchInput_.length()) {
    int16_t caretX = 72 + PokedexText::width(g, fitted.c_str(), PokedexText::fontXL()) + 5;
    if (caretX < 940) g->fillRect(caretX, 160, 3, 34, Widgets::rgb(0x16, 0xC2, 0xC9));
  }
  drawButton(g, 820, 112, 132, "CANCEL", kDexRedDark);
  PokedexText::draw(g, 72, 232, "RETURN searches   BACK deletes   123 switches symbols", PokedexText::fontS(),
                    kMuted, PokedexText::kLeft);
  keyboard_.draw(g);
}

void PokedexDashboard::draw() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  g->fillScreen(Widgets::kBg);
  if (mode_ == kPokedexUiDetail && detail_.loaded) {
    drawDetail(g);
  } else if (mode_ == kPokedexUiSearch) {
    drawSearch(g);
  } else {
    drawGrid(g);
  }
}

int16_t PokedexDashboard::calibrateX(int16_t rawX, int16_t rawY) const {
  int16_t source = POKEDEX_TOUCH_SWAP_XY ? rawY : rawX;
  int16_t value = mapAxis(source, POKEDEX_TOUCH_MIN_X, POKEDEX_TOUCH_MAX_X, kScreenW - 1);
  if (POKEDEX_TOUCH_INVERT_X) value = kScreenW - 1 - value;
  return value;
}

int16_t PokedexDashboard::calibrateY(int16_t rawX, int16_t rawY) const {
  int16_t source = POKEDEX_TOUCH_SWAP_XY ? rawX : rawY;
  int16_t value = mapAxis(source, POKEDEX_TOUCH_MIN_Y, POKEDEX_TOUCH_MAX_Y, kScreenH - 1);
  if (POKEDEX_TOUCH_INVERT_Y) value = kScreenH - 1 - value;
  return value;
}

bool PokedexDashboard::handleTouchMapped(int16_t x, int16_t y, PokedexUiEvent &event) {
  event = PokedexUiEvent();
  if (mode_ == kPokedexUiSearch) {
    if (inside(x, y, 808, 104, 160, 70)) {
      event.type = kPokedexUiSearchCancel;
      return true;
    }

    PokedexKeyEvent key = keyboard_.hitTest(x, y);
    if (key.action == kPokedexKeyNone) return false;
    if (key.action == kPokedexKeyShift || key.action == kPokedexKeySymbols) {
      keyboard_.applyModeAction(key.action);
    } else if (key.action == kPokedexKeyText && searchInput_.length() < 64) {
      searchInput_ = searchInput_.substring(0, searchCursor_) + key.text +
                     searchInput_.substring(searchCursor_);
      searchCursor_ += key.text.length();
      if (keyboard_.shifted()) keyboard_.applyModeAction(kPokedexKeyShift);
    } else if (key.action == kPokedexKeyBackspace && searchCursor_ > 0) {
      searchInput_.remove(searchCursor_ - 1, 1);
      --searchCursor_;
    } else if (key.action == kPokedexKeyLeft && searchCursor_ > 0) {
      --searchCursor_;
    } else if (key.action == kPokedexKeyRight && searchCursor_ < searchInput_.length()) {
      ++searchCursor_;
    } else if (key.action == kPokedexKeyEnter) {
      event.type = kPokedexUiSearchSubmit;
      event.text = searchInput_;
      return true;
    }
    dirty_ = true;
    return true;
  }
  if (mode_ == kPokedexUiList) {
    // Footer buttons. Regions are deliberately taller than the drawn button so
    // a slightly low tap still lands.
    if (inside(x, y, 16, kFooterY - 20, 112, 76)) {
      event.type = kPokedexUiJumpTop;
      return true;
    }
    if (inside(x, y, 120, kFooterY - 20, 120, 76)) {
      event.type = kPokedexUiBrowsePrev;
      return true;
    }
    if (inside(x, y, 232, kFooterY - 20, 120, 76)) {
      event.type = kPokedexUiBrowseNext;
      return true;
    }
    if (inside(x, y, 344, kFooterY - 20, 134, 76)) {
      event.type = kPokedexUiOpenSearch;
      return true;
    }
    if (inside(x, y, 470, kFooterY - 20, 120, 76)) {
      event.type = kPokedexUiToggleSort;
      return true;
    }
    if (inside(x, y, 582, kFooterY - 20, 146, 76)) {
      event.type = kPokedexUiCycleType;
      return true;
    }
    if (inside(x, y, 720, kFooterY - 20, 156, 76)) {
      event.type = kPokedexUiToggleShadows;
      return true;
    }

    // Jump rail. Dimmed slots are not tappable.
    if (inside(x, y, kRailX, kRailY, kRailW, kRailH)) {
      const uint8_t slots = (order_ == pokedex::kOrderName) ? kRailSlots : kDexBuckets;
      const int16_t slotH = kRailH / slots;
      const int16_t hit = (y - kRailY) / slotH;
      if (hit >= 0 && hit < (int16_t)slots && railEnabled_[hit]) {
        if (order_ == pokedex::kOrderName) {
          event.type = kPokedexUiJumpLetter;
          event.letter = (char)('A' + hit);
        } else {
          event.type = kPokedexUiJumpDex;
          event.dex = (uint16_t)(hit * 100);
        }
        return true;
      }
      return false;
    }

    // Grid tiles. The whole ~154x140 cell is the target, not just the sprite.
    if (inside(x, y, kGridX, kGridY, kGridW, kGridH)) {
      const int16_t col = (x - kGridX) / kGridCellW;
      const int16_t rowIndex = (y - kGridY) / kGridCellH;
      if (col >= 0 && col < kGridCols && rowIndex >= 0 && rowIndex < kGridRows) {
        const uint8_t slot = (uint8_t)(rowIndex * kGridCols + col);
        if (slot < rowCount_) {
          event.type = kPokedexUiSelectRow;
          event.row = slot;
          return true;
        }
      }
      return false;
    }
  } else {
    if (inside(x, y, 16, kFooterY - 20, 128, 76)) {
      event.type = kPokedexUiBackToList;
      return true;
    }
    for (uint8_t i = 0; i < POKEDEX_DETAIL_PAGE_COUNT; i++) {
      const int16_t x0 = 160 + i * 168;
      if (inside(x, y, x0, kFooterY - 20, 174, 76)) {
        event.type = kPokedexUiSelectTab;
        event.tab = i;
        return true;
      }
    }
  }
  return false;
}

bool PokedexDashboard::handleTouch(int16_t rawX, int16_t rawY, PokedexUiEvent &event) {
  lastRawX_ = rawX;
  lastRawY_ = rawY;
  lastTouchX_ = calibrateX(rawX, rawY);
  lastTouchY_ = calibrateY(rawX, rawY);
  showTouchDot_ = true;
  touchCount_++;
  dirty_ = true;

  Logger::info("touch", String("raw=") + rawX + "," + rawY + " mapped=" +
                            lastTouchX_ + "," + lastTouchY_ + " count=" + touchCount_);

  if (handleTouchMapped(lastTouchX_, lastTouchY_, event)) {
    return true;
  }

#if POKEDEX_TOUCH_AUTO_REMAP
  const int16_t variants[][2] = {
      {rawX, rawY},
      {(int16_t)(kScreenW - 1 - rawX), rawY},
      {rawX, (int16_t)(kScreenH - 1 - rawY)},
      {(int16_t)(kScreenW - 1 - rawX), (int16_t)(kScreenH - 1 - rawY)},
      {rawY, rawX},
      {(int16_t)(kScreenW - 1 - rawY), rawX},
      {rawY, (int16_t)(kScreenH - 1 - rawX)},
      {(int16_t)(kScreenW - 1 - rawY), (int16_t)(kScreenH - 1 - rawX)},
  };
  for (uint8_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
    int16_t x = clampCoord(variants[i][0], kScreenW - 1);
    int16_t y = clampCoord(variants[i][1], kScreenH - 1);
    if (x == lastTouchX_ && y == lastTouchY_) continue;
    if (handleTouchMapped(x, y, event)) {
      lastTouchX_ = x;
      lastTouchY_ = y;
      Logger::info("touch", String("auto-remap hit mapped=") + x + "," + y);
      return true;
    }
  }
#endif

  Logger::info("touch", "tap missed all Pokedex targets");
  return false;
}

#endif
