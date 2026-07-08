#include "PokedexDashboard.h"

#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

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
  rowCount_ = min(count, (uint8_t)POKEDEX_MAX_RESULTS);
  for (uint8_t i = 0; i < rowCount_; i++) {
    rows_[i] = rows[i];
  }
  selected_ = (rowCount_ == 0) ? 0 : min(selected, (uint8_t)(rowCount_ - 1));
  browseStart_ = browseStart;
  totalRows_ = totalRows;
  query_ = query;
  source_ = source;
  status_ = status;
  mode_ = kPokedexUiList;
  dirty_ = true;
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

String fitText(Arduino_GFX *g, String text, int16_t maxW, const GFXfont *font) {
  text.trim();
  if (Widgets::textWidth(g, text.c_str(), font) <= maxW) {
    return text;
  }
  while (text.length() > 3 && Widgets::textWidth(g, (text + "...").c_str(), font) > maxW) {
    text.remove(text.length() - 1);
  }
  text += "...";
  return text;
}

void label(Arduino_GFX *g, int16_t x, int16_t y, const char *text) {
  Widgets::text(g, x, y, text, Widgets::fontS(), kMuted, Widgets::kLeft);
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
  Widgets::text(g, x + w / 2, y + 9, text, Widgets::fontS(), kWhite, Widgets::kCenter);
}

void drawTypeChip(Arduino_GFX *g, int16_t x, int16_t y, const char *type) {
  if (type == nullptr || type[0] == '\0') return;
  Widgets::pill(g, x, y, type, Widgets::fontS(), kInk, typeColor(type));
}

void drawWrapped(Arduino_GFX *g, String text, int16_t x, int16_t y, int16_t w, int16_t lineH,
                 uint8_t maxLines, const GFXfont *font, uint16_t color) {
  text.trim();
  if (text.length() == 0) text = "-";
  for (uint8_t line = 0; line < maxLines && text.length() > 0; line++) {
    String chunk = text;
    while (chunk.length() > 0 && Widgets::textWidth(g, chunk.c_str(), font) > w) {
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
    Widgets::text(g, x, y + line * lineH, chunk.c_str(), font, color, Widgets::kLeft);
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
  Widgets::text(g, x + 74, y - 2, buf, Widgets::fontS(), kWhite, Widgets::kRight);
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
  Widgets::text(g, 92, 20, title, Widgets::fontL(), kWhite, Widgets::kLeft);
  Widgets::text(g, 92, 52, source_.c_str(), Widgets::fontS(), Widgets::rgb(0xFE, 0xF3, 0xC7),
                Widgets::kLeft);
  Widgets::pill(g, 778, 27, status_.c_str(), Widgets::fontS(), kInk,
                status_.indexOf("SD") >= 0 ? Widgets::kGreen : kGold);
}

void PokedexDashboard::drawFooter(Arduino_GFX *g) {
  g->fillRect(0, kFooterY - 12, kScreenW, kScreenH - kFooterY + 12, Widgets::kBg);
  if (mode_ == kPokedexUiList) {
    drawButton(g, 24, kFooterY, 126, "PREV");
    drawButton(g, 166, kFooterY, 126, "NEXT");
    Widgets::text(g, 320, kFooterY + 8, "Tap a row to open. Serial: search, browse, open.",
                  Widgets::fontS(), kMuted, Widgets::kLeft);
  } else {
    drawButton(g, 24, kFooterY, 126, "LIST");
    drawButton(g, 764, kFooterY, 112, "PAGE -");
    drawButton(g, 892, kFooterY, 112, "PAGE +", kDexRedDark);
    char page[24];
    snprintf(page, sizeof(page), "Page %u/%u", detailPage_ + 1, POKEDEX_DETAIL_PAGE_COUNT);
    Widgets::text(g, 512, kFooterY + 8, page, Widgets::fontS(), kMuted, Widgets::kCenter);
  }
}

void PokedexDashboard::drawTouchDot(Arduino_GFX *g) {
  if (!showTouchDot_) return;
  g->fillCircle(lastTouchX_, lastTouchY_, 11, kInk);
  g->drawCircle(lastTouchX_, lastTouchY_, 11, kGold);
  g->drawLine(lastTouchX_ - 15, lastTouchY_, lastTouchX_ + 15, lastTouchY_, kGold);
  g->drawLine(lastTouchX_, lastTouchY_ - 15, lastTouchX_, lastTouchY_ + 15, kGold);
}

void PokedexDashboard::drawList(Arduino_GFX *g) {
  drawHeader(g, "POKEDEX PANEL");
  Widgets::panel(g, kListX, kListY, kListW, kListH, 8, kCard, 1, kLine);
  Widgets::text(g, kListX + 20, kListY + 18, "CATALOG", Widgets::fontS(), kGold, Widgets::kLeft);

  char range[48];
  uint16_t end = (rowCount_ == 0) ? 0 : (browseStart_ + rowCount_);
  snprintf(range, sizeof(range), "%u-%u of %u", (rowCount_ == 0) ? 0 : browseStart_ + 1, end,
           totalRows_);
  Widgets::text(g, kListX + kListW - 20, kListY + 18, range, Widgets::fontS(), kMuted,
                Widgets::kRight);

  if (rowCount_ == 0) {
    Widgets::text(g, kListX + 24, kListY + 88, "No matches", Widgets::fontL(), kWhite,
                  Widgets::kLeft);
    drawWrapped(g, "Try a name, dex number, type, or variant tag from Serial.",
                kListX + 24, kListY + 134, kListW - 48, 28, 4, Widgets::fontS(), kMuted);
  }

  for (uint8_t i = 0; i < rowCount_; i++) {
    int16_t y = kListY + 58 + i * kRowH;
    bool active = i == selected_;
    uint16_t fill = active ? Widgets::rgb(0x27, 0x38, 0x4C) : Widgets::rgb(0x10, 0x17, 0x21);
    Widgets::panel(g, kListX + 14, y, kListW - 28, 38, 8, fill, active ? 2 : 1,
                   active ? typeColor(rows_[i].type1) : kLine);
    char dex[12];
    snprintf(dex, sizeof(dex), "#%s", rows_[i].dex);
    Widgets::text(g, kListX + 30, y + 9, dex, Widgets::fontS(), typeColor(rows_[i].type1),
                  Widgets::kLeft);
    String name = fitText(g, rows_[i].name, 165, Widgets::fontS());
    Widgets::text(g, kListX + 91, y + 9, name.c_str(), Widgets::fontS(), kWhite, Widgets::kLeft);
    Widgets::text(g, kListX + kListW - 32, y + 9, rows_[i].type1, Widgets::fontS(), kMuted,
                  Widgets::kRight);
  }

  Widgets::panel(g, kHeroX, kHeroY, kHeroW, kHeroH, 8, kCard, 1, kLine);
  Widgets::text(g, kHeroX + 28, kHeroY + 24, "READY TO SCAN", Widgets::fontS(), kGold,
                Widgets::kLeft);
  drawPokeball(g, kHeroX + kHeroW / 2, kHeroY + 190, 112, kDexRed);
  drawWrapped(g, String("Query: ") + query_, kHeroX + 36, kHeroY + 332, kHeroW - 72, 30, 2,
              Widgets::fontM(), kWhite);
  drawWrapped(g, "Use search pikachu, search mega, browse 24, or open mewtwo.",
              kHeroX + 36, kHeroY + 382, kHeroW - 72, 24, 2, Widgets::fontS(), kMuted);

  Widgets::panel(g, kSideX, kSideY, kSideW, kSideH, 8, kCard, 1, kLine);
  Widgets::text(g, kSideX + 20, kSideY + 20, "PORT NOTES", Widgets::fontS(), kGold,
                Widgets::kLeft);
  drawWrapped(g, "Source: local esp32-pokedex. The original Cardputer keyboard and audio paths were replaced with touch and Serial controls.",
              kSideX + 20, kSideY + 68, kSideW - 40, 26, 6, Widgets::fontS(), kMuted);
  drawWrapped(g, "SD mode streams index.csv and per-Pokemon JSON. Mock mode keeps the demo live without a card.",
              kSideX + 20, kSideY + 252, kSideW - 40, 26, 5, Widgets::fontS(), kWhite);
  drawFooter(g);
  drawTouchDot(g);
}

void PokedexDashboard::drawDetail(Arduino_GFX *g) {
  drawHeader(g, "POKEDEX DETAIL");
  uint16_t primary = typeColor(detail_.type1);

  Widgets::panel(g, kListX, kListY, kListW, kListH, 8, kCard, 1, kLine);
  Widgets::text(g, kListX + 24, kListY + 24, "ENTRY", Widgets::fontS(), kGold, Widgets::kLeft);
  char title[96];
  snprintf(title, sizeof(title), "#%s %s", detail_.dex, detail_.name);
  drawWrapped(g, title, kListX + 24, kListY + 72, kListW - 48, 36, 2, Widgets::fontL(), kWhite);
  drawTypeChip(g, kListX + 24, kListY + 156, detail_.type1);
  drawTypeChip(g, kListX + 148, kListY + 156, detail_.type2);
  label(g, kListX + 24, kListY + 214, "TAGS");
  drawWrapped(g, detail_.tags, kListX + 24, kListY + 246, kListW - 48, 24, 3, Widgets::fontS(),
              kMuted);
  label(g, kListX + 24, kListY + 344, "SOURCE");
  drawWrapped(g, detail_.sourceFile, kListX + 24, kListY + 374, kListW - 48, 24, 2,
              Widgets::fontS(), kMuted);

  Widgets::panel(g, kHeroX, kHeroY, kHeroW, kHeroH, 8, kCard, 1, kLine);
  drawPokeball(g, kHeroX + kHeroW / 2, kHeroY + 150, 110, primary);
  statBar(g, kHeroX + 42, kHeroY + 288, "ATK", detail_.atk, kDexRed);
  statBar(g, kHeroX + 42, kHeroY + 328, "DEF", detail_.def, kBlue);
  statBar(g, kHeroX + 42, kHeroY + 368, "HP", detail_.hp, Widgets::kGreen);
  char buddy[64];
  snprintf(buddy, sizeof(buddy), "Buddy %ukm   2nd move %lu dust", detail_.buddyKm,
           (unsigned long)detail_.secondMoveStardust);
  Widgets::text(g, kHeroX + kHeroW / 2, kHeroY + 402, buddy, Widgets::fontS(), kMuted,
                Widgets::kCenter);

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
  Widgets::text(g, kSideX + 20, kSideY + 20, section, Widgets::fontS(), kGold, Widgets::kLeft);
  drawWrapped(g, body, kSideX + 20, kSideY + 72, kSideW - 40, 28, 10, Widgets::fontS(), kWhite);
  drawFooter(g);
  drawTouchDot(g);
}

void PokedexDashboard::draw() {
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g == nullptr) return;
  g->fillScreen(Widgets::kBg);
  if (mode_ == kPokedexUiDetail && detail_.loaded) {
    drawDetail(g);
  } else {
    drawList(g);
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
  if (mode_ == kPokedexUiList) {
    if (inside(x, y, 0, kFooterY - 20, 164, 76)) {
      event.type = kPokedexUiBrowsePrev;
      return true;
    }
    if (inside(x, y, 152, kFooterY - 20, 164, 76)) {
      event.type = kPokedexUiBrowseNext;
      return true;
    }
    for (uint8_t i = 0; i < rowCount_; i++) {
      int16_t rowY = kListY + 58 + i * kRowH;
      if (inside(x, y, kListX, rowY - 8, kListW, kRowH)) {
        event.type = kPokedexUiSelectRow;
        event.row = i;
        return true;
      }
    }
    if (inside(x, y, kHeroX, kHeroY, kHeroW, kHeroH) && rowCount_ > 0) {
      event.type = kPokedexUiSelectRow;
      event.row = selected_;
      return true;
    }
  } else {
    if (inside(x, y, 0, kFooterY - 20, 164, 76)) {
      event.type = kPokedexUiBackToList;
      return true;
    }
    if (inside(x, y, 736, kFooterY - 20, 152, 76)) {
      event.type = kPokedexUiPrevPage;
      return true;
    }
    if (inside(x, y, 876, kFooterY - 20, 148, 76)) {
      event.type = kPokedexUiNextPage;
      return true;
    }
    if (inside(x, y, kSideX - 24, kSideY - 12, kSideW + 48, kSideH + 24) ||
        inside(x, y, kHeroX, kHeroY, kHeroW, kHeroH)) {
      event.type = kPokedexUiNextPage;
      return true;
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
