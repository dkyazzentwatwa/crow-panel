#include "TouchKeyboard.h"

namespace {
struct Key {
  const char *label;  // null for a plain character key (ch is rendered)
  KbAction action;
  char ch;
  float weight;
};

// Geometry (1024x600 panel, keyboard in the bottom ~290px).
constexpr int16_t kKbMarginX = 12;
constexpr int16_t kKbY0 = 306;
constexpr int16_t kKbRowH = 66;
constexpr int16_t kKbGap = 8;
constexpr int16_t kKbW = 1024 - 2 * kKbMarginX;

#define CH(c) {nullptr, KB_CHAR, (c), 1.0f}

const Key kLettersR0[] = {CH('q'), CH('w'), CH('e'), CH('r'), CH('t'),
                          CH('y'), CH('u'), CH('i'), CH('o'), CH('p')};
const Key kLettersR1[] = {CH('a'), CH('s'), CH('d'), CH('f'), CH('g'),
                          CH('h'), CH('j'), CH('k'), CH('l')};
const Key kLettersR2[] = {{"shift", KB_SHIFT, 0, 1.5f}, CH('z'), CH('x'), CH('c'),
                          CH('v'), CH('b'), CH('n'), CH('m'),
                          {"del", KB_BACKSPACE, 0, 1.5f}};
const Key kRow3Letters[] = {{"123", KB_SYMBOLS, 0, 1.5f}, {"space", KB_CHAR, ' ', 5.0f},
                            {"cancel", KB_CANCEL, 0, 1.75f}, {"enter", KB_ENTER, 0, 1.75f}};

const Key kSymR0[] = {CH('1'), CH('2'), CH('3'), CH('4'), CH('5'),
                      CH('6'), CH('7'), CH('8'), CH('9'), CH('0')};
const Key kSymR1[] = {CH('-'), CH('/'), CH(':'), CH('('), CH(')'),
                      CH('$'), CH('&'), CH('@'), CH('=')};
const Key kSymR2[] = {{"shift", KB_SHIFT, 0, 1.5f}, CH('.'), CH(','), CH('?'),
                      CH('!'), CH('\''), CH('"'), CH(';'),
                      {"del", KB_BACKSPACE, 0, 1.5f}};
const Key kRow3Sym[] = {{"ABC", KB_SYMBOLS, 0, 1.5f}, {"space", KB_CHAR, ' ', 5.0f},
                        {"cancel", KB_CANCEL, 0, 1.75f}, {"enter", KB_ENTER, 0, 1.75f}};

#undef CH

struct Row {
  const Key *keys;
  uint8_t count;
};

void rowsFor(bool symbols, Row out[4]) {
  if (symbols) {
    out[0] = {kSymR0, 10};
    out[1] = {kSymR1, 9};
    out[2] = {kSymR2, 9};
    out[3] = {kRow3Sym, 4};
  } else {
    out[0] = {kLettersR0, 10};
    out[1] = {kLettersR1, 9};
    out[2] = {kLettersR2, 9};
    out[3] = {kRow3Letters, 4};
  }
}

// Rect for key i in a row, computed identically for draw and hit-test.
void keyRect(const Row &row, uint8_t i, int16_t rowY, int16_t &x, int16_t &y, int16_t &w,
             int16_t &h) {
  float total = 0;
  for (uint8_t k = 0; k < row.count; ++k) total += row.keys[k].weight;
  float unit = (kKbW - kKbGap * (row.count - 1)) / total;
  float cx = kKbMarginX;
  for (uint8_t k = 0; k < i; ++k) cx += row.keys[k].weight * unit + kKbGap;
  x = (int16_t)cx;
  y = rowY;
  w = (int16_t)(row.keys[i].weight * unit);
  h = kKbRowH - kKbGap;
}
}  // namespace

KbEvent TouchKeyboard::hitTest(int16_t px, int16_t py) {
  KbEvent ev;
  Row rows[4];
  rowsFor(symbols_, rows);
  for (uint8_t r = 0; r < 4; ++r) {
    int16_t rowY = kKbY0 + r * kKbRowH;
    if (py < rowY || py >= rowY + kKbRowH) continue;
    for (uint8_t i = 0; i < rows[r].count; ++i) {
      int16_t x, y, w, h;
      keyRect(rows[r], i, rowY, x, y, w, h);
      if (px < x || px >= x + w) continue;
      const Key &key = rows[r].keys[i];
      switch (key.action) {
        case KB_SHIFT:
          shift_ = !shift_;
          ev.action = KB_SHIFT;
          return ev;
        case KB_SYMBOLS:
          symbols_ = !symbols_;
          ev.action = KB_SYMBOLS;
          return ev;
        case KB_CHAR: {
          char c = key.ch;
          if (shift_ && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
            shift_ = false;  // one-shot
          }
          ev.action = KB_CHAR;
          ev.ch = c;
          return ev;
        }
        default:
          ev.action = key.action;
          return ev;
      }
    }
  }
  return ev;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <DashboardWidgets.h>
using namespace Widgets;

void TouchKeyboard::draw(Arduino_GFX *g) const {
  Row rows[4];
  rowsFor(symbols_, rows);
  for (uint8_t r = 0; r < 4; ++r) {
    int16_t rowY = kKbY0 + r * kKbRowH;
    for (uint8_t i = 0; i < rows[r].count; ++i) {
      int16_t x, y, w, h;
      keyRect(rows[r], i, rowY, x, y, w, h);
      const Key &key = rows[r].keys[i];
      bool special = key.action != KB_CHAR;
      uint16_t fill = kSurface;
      uint16_t fg = kTextHi;
      if (key.action == KB_ENTER) { fill = kAccent; fg = kBg; }
      else if (key.action == KB_CANCEL) { fill = kSurfaceHi; fg = kRed; }
      else if (special) { fill = kSurfaceHi; fg = kTextHi; }
      if (key.action == KB_SHIFT && shift_) { fill = kAccent; fg = kBg; }
      panel(g, x, y, w, h, 8, fill, 1, kLine);
      if (key.label) {
        text(g, x + w / 2, y + h / 2 - 6, key.label, fontS(), fg, kCenter);
      } else {
        char s[2] = {key.ch, 0};
        if (shift_ && key.ch >= 'a' && key.ch <= 'z') s[0] = (char)(key.ch - 'a' + 'A');
        text(g, x + w / 2, y + h / 2 - 8, s, fontL(), fg, kCenter);
      }
    }
  }
}
#endif
