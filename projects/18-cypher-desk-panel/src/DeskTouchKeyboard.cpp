#include "DeskTouchKeyboard.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <U8g2lib.h>
#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#endif

namespace {

constexpr int16_t kKeyboardY = 316;
constexpr int16_t kKeyboardMarginX = 22;
constexpr int16_t kKeyboardWidth = 980;
constexpr int16_t kRowHeight = 60;
constexpr int16_t kRowGap = 8;
constexpr int16_t kKeyGap = 6;

struct KeyDefinition {
  const char *label;
  const char *text;
  DeskKeyAction action;
  uint8_t weight;
};

const KeyDefinition kLetters0[] = {
    {"Q", "q", kDeskKeyText, 10}, {"W", "w", kDeskKeyText, 10},
    {"E", "e", kDeskKeyText, 10}, {"R", "r", kDeskKeyText, 10},
    {"T", "t", kDeskKeyText, 10}, {"Y", "y", kDeskKeyText, 10},
    {"U", "u", kDeskKeyText, 10}, {"I", "i", kDeskKeyText, 10},
    {"O", "o", kDeskKeyText, 10}, {"P", "p", kDeskKeyText, 10}};
const KeyDefinition kLetters1[] = {
    {"A", "a", kDeskKeyText, 10}, {"S", "s", kDeskKeyText, 10},
    {"D", "d", kDeskKeyText, 10}, {"F", "f", kDeskKeyText, 10},
    {"G", "g", kDeskKeyText, 10}, {"H", "h", kDeskKeyText, 10},
    {"J", "j", kDeskKeyText, 10}, {"K", "k", kDeskKeyText, 10},
    {"L", "l", kDeskKeyText, 10}};
const KeyDefinition kLetters2[] = {
    {"SHIFT", "", kDeskKeyShift, 15}, {"Z", "z", kDeskKeyText, 10},
    {"X", "x", kDeskKeyText, 10}, {"C", "c", kDeskKeyText, 10},
    {"V", "v", kDeskKeyText, 10}, {"B", "b", kDeskKeyText, 10},
    {"N", "n", kDeskKeyText, 10}, {"M", "m", kDeskKeyText, 10},
    {"BACK", "", kDeskKeyBackspace, 15}};
const KeyDefinition kLetters3[] = {
    {"123", "", kDeskKeySymbols, 14}, {",", ",", kDeskKeyText, 8},
    {"SPACE", " ", kDeskKeyText, 42}, {".", ".", kDeskKeyText, 8},
    {"LEFT", "", kDeskKeyLeft, 10}, {"RIGHT", "", kDeskKeyRight, 10},
    {"RETURN", "", kDeskKeyEnter, 18}};

const KeyDefinition kSymbols0[] = {
    {"1", "1", kDeskKeyText, 10}, {"2", "2", kDeskKeyText, 10},
    {"3", "3", kDeskKeyText, 10}, {"4", "4", kDeskKeyText, 10},
    {"5", "5", kDeskKeyText, 10}, {"6", "6", kDeskKeyText, 10},
    {"7", "7", kDeskKeyText, 10}, {"8", "8", kDeskKeyText, 10},
    {"9", "9", kDeskKeyText, 10}, {"0", "0", kDeskKeyText, 10}};
const KeyDefinition kSymbols1[] = {
    {"-", "-", kDeskKeyText, 10}, {"/", "/", kDeskKeyText, 10},
    {":", ":", kDeskKeyText, 10}, {";", ";", kDeskKeyText, 10},
    {"(", "(", kDeskKeyText, 10}, {")", ")", kDeskKeyText, 10},
    {"$", "$", kDeskKeyText, 10}, {"&", "&", kDeskKeyText, 10},
    {"@", "@", kDeskKeyText, 10}, {"#", "#", kDeskKeyText, 10}};
const KeyDefinition kSymbols2[] = {
    {"ABC", "", kDeskKeySymbols, 15}, {".", ".", kDeskKeyText, 10},
    {",", ",", kDeskKeyText, 10}, {"?", "?", kDeskKeyText, 10},
    {"!", "!", kDeskKeyText, 10}, {"'", "'", kDeskKeyText, 10},
    {"\"", "\"", kDeskKeyText, 10}, {"_", "_", kDeskKeyText, 10},
    {"BACK", "", kDeskKeyBackspace, 15}};
const KeyDefinition kSymbols3[] = {
    {"ABC", "", kDeskKeySymbols, 14}, {"[", "[", kDeskKeyText, 8},
    {"SPACE", " ", kDeskKeyText, 42}, {"]", "]", kDeskKeyText, 8},
    {"LEFT", "", kDeskKeyLeft, 10}, {"RIGHT", "", kDeskKeyRight, 10},
    {"RETURN", "", kDeskKeyEnter, 18}};

struct KeyboardRow {
  const KeyDefinition *keys;
  uint8_t count;
};

KeyboardRow rowAt(bool symbols, uint8_t row) {
  if (!symbols) {
    if (row == 0) return {kLetters0, 10};
    if (row == 1) return {kLetters1, 9};
    if (row == 2) return {kLetters2, 9};
    return {kLetters3, 7};
  }
  if (row == 0) return {kSymbols0, 10};
  if (row == 1) return {kSymbols1, 10};
  if (row == 2) return {kSymbols2, 9};
  return {kSymbols3, 7};
}

int16_t rowInset(uint8_t row) {
  return row == 1 ? 44 : 0;
}

int16_t rowWidth(uint8_t row) {
  return kKeyboardWidth - rowInset(row) * 2;
}

uint16_t totalWeight(const KeyboardRow &row) {
  uint16_t total = 0;
  for (uint8_t i = 0; i < row.count; ++i) total += row.keys[i].weight;
  return total;
}

bool keyBounds(const KeyboardRow &row, uint8_t rowIndex, uint8_t keyIndex,
               int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  if (keyIndex >= row.count) return false;
  int16_t available = rowWidth(rowIndex) - (row.count - 1) * kKeyGap;
  uint16_t weight = totalWeight(row);
  x = kKeyboardMarginX + rowInset(rowIndex);
  for (uint8_t i = 0; i < keyIndex; ++i) {
    x += (int32_t)available * row.keys[i].weight / weight + kKeyGap;
  }
  w = (int32_t)available * row.keys[keyIndex].weight / weight;
  y = kKeyboardY + rowIndex * (kRowHeight + kRowGap);
  h = kRowHeight;
  return true;
}

bool inside(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

}  // namespace

void DeskTouchKeyboard::reset() {
  shifted_ = false;
  symbols_ = false;
}

bool DeskTouchKeyboard::shifted() const {
  return shifted_;
}

bool DeskTouchKeyboard::symbols() const {
  return symbols_;
}

void DeskTouchKeyboard::applyModeAction(DeskKeyAction action) {
  if (action == kDeskKeyShift) shifted_ = !shifted_;
  if (action == kDeskKeySymbols) {
    symbols_ = !symbols_;
    shifted_ = false;
  }
}

DeskKeyEvent DeskTouchKeyboard::hitTest(int16_t x, int16_t y) const {
  DeskKeyEvent event;
  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      int16_t keyX, keyY, keyW, keyH;
      keyBounds(row, rowIndex, keyIndex, keyX, keyY, keyW, keyH);
      // Extend each logical target into half of the visual gutter. This removes
      // dead strips between keys while preserving the visible breathing room.
      if (!inside(x, y, keyX - kKeyGap / 2, keyY - kRowGap / 2,
                  keyW + kKeyGap, keyH + kRowGap)) continue;
      event.action = row.keys[keyIndex].action;
      event.text = row.keys[keyIndex].text;
      if (event.action == kDeskKeyText && shifted_ && event.text.length() == 1 &&
          isalpha((unsigned char)event.text[0])) {
        event.text[0] = toupper(event.text[0]);
      }
      return event;
    }
  }
  return event;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
void drawSmallCentered(Arduino_GFX *g, int16_t centerX, int16_t topY,
                       const char *label, uint16_t color) {
  g->setFont(u8g2_font_cubic11_h_cjk);
  g->setUTF8Print(true);
  g->setTextSize(1);
  g->setTextColor(color);
  int16_t bx, by;
  uint16_t bw, bh;
  g->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  g->setCursor(centerX - (int16_t)bw / 2 - bx, topY - by);
  g->print(label);
}

void DeskTouchKeyboard::draw(Arduino_GFX *g, const DeskThemePalette &theme) const {
  if (g == nullptr) return;
  const uint16_t actionFill = theme.panelHighlight;
  const uint16_t activeFill = theme.accent2;
  const uint16_t lineRows[] = {theme.accent, theme.accent3, theme.accent2, theme.success};
  const uint16_t shadow = theme.background;
  const uint16_t keyboardBg = theme.shell;
  g->fillRect(0, kKeyboardY - 12, 1024, 296, keyboardBg);
  g->fillRoundRect(430, kKeyboardY - 8, 164, 5, 3,
                   theme.accent);

  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      const KeyDefinition &key = row.keys[keyIndex];
      int16_t x, y, w, h;
      keyBounds(row, rowIndex, keyIndex, x, y, w, h);
      bool modeActive = (key.action == kDeskKeyShift && shifted_) ||
                        (key.action == kDeskKeySymbols && symbols_);
      uint16_t fill = modeActive ? activeFill :
                      (key.action == kDeskKeyText ? theme.keyboardRows[rowIndex] : actionFill);
      Widgets::panel(g, x + 3, y + 4, w, h, 11, shadow);
      Widgets::panel(g, x, y, w, h, 11, fill, modeActive ? 3 : 1,
                     lineRows[rowIndex]);
      if (strlen(key.label) <= 2) {
        Widgets::text(g, x + w / 2, y + 17, key.label, Widgets::fontM(),
                      theme.ink, Widgets::kCenter);
      } else {
        drawSmallCentered(g, x + w / 2, y + 21, key.label,
                          theme.ink);
      }
    }
  }
}
#endif
