#include "PokedexTouchKeyboard.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#include "PokedexText.h"
#include <cctype>
#include <cstring>
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
  PokedexKeyAction action;
  uint8_t weight;
};

const KeyDefinition kLetters0[] = {
    {"Q", "q", kPokedexKeyText, 10}, {"W", "w", kPokedexKeyText, 10},
    {"E", "e", kPokedexKeyText, 10}, {"R", "r", kPokedexKeyText, 10},
    {"T", "t", kPokedexKeyText, 10}, {"Y", "y", kPokedexKeyText, 10},
    {"U", "u", kPokedexKeyText, 10}, {"I", "i", kPokedexKeyText, 10},
    {"O", "o", kPokedexKeyText, 10}, {"P", "p", kPokedexKeyText, 10}};
const KeyDefinition kLetters1[] = {
    {"A", "a", kPokedexKeyText, 10}, {"S", "s", kPokedexKeyText, 10},
    {"D", "d", kPokedexKeyText, 10}, {"F", "f", kPokedexKeyText, 10},
    {"G", "g", kPokedexKeyText, 10}, {"H", "h", kPokedexKeyText, 10},
    {"J", "j", kPokedexKeyText, 10}, {"K", "k", kPokedexKeyText, 10},
    {"L", "l", kPokedexKeyText, 10}};
const KeyDefinition kLetters2[] = {
    {"SHIFT", "", kPokedexKeyShift, 15}, {"Z", "z", kPokedexKeyText, 10},
    {"X", "x", kPokedexKeyText, 10}, {"C", "c", kPokedexKeyText, 10},
    {"V", "v", kPokedexKeyText, 10}, {"B", "b", kPokedexKeyText, 10},
    {"N", "n", kPokedexKeyText, 10}, {"M", "m", kPokedexKeyText, 10},
    {"BACK", "", kPokedexKeyBackspace, 15}};
const KeyDefinition kLetters3[] = {
    {"123", "", kPokedexKeySymbols, 14}, {",", ",", kPokedexKeyText, 8},
    {"SPACE", " ", kPokedexKeyText, 42}, {".", ".", kPokedexKeyText, 8},
    {"LEFT", "", kPokedexKeyLeft, 10}, {"RIGHT", "", kPokedexKeyRight, 10},
    {"RETURN", "", kPokedexKeyEnter, 18}};

const KeyDefinition kSymbols0[] = {
    {"1", "1", kPokedexKeyText, 10}, {"2", "2", kPokedexKeyText, 10},
    {"3", "3", kPokedexKeyText, 10}, {"4", "4", kPokedexKeyText, 10},
    {"5", "5", kPokedexKeyText, 10}, {"6", "6", kPokedexKeyText, 10},
    {"7", "7", kPokedexKeyText, 10}, {"8", "8", kPokedexKeyText, 10},
    {"9", "9", kPokedexKeyText, 10}, {"0", "0", kPokedexKeyText, 10}};
const KeyDefinition kSymbols1[] = {
    {"-", "-", kPokedexKeyText, 10}, {"/", "/", kPokedexKeyText, 10},
    {":", ":", kPokedexKeyText, 10}, {";", ";", kPokedexKeyText, 10},
    {"(", "(", kPokedexKeyText, 10}, {")", ")", kPokedexKeyText, 10},
    {"$", "$", kPokedexKeyText, 10}, {"&", "&", kPokedexKeyText, 10},
    {"@", "@", kPokedexKeyText, 10}, {"#", "#", kPokedexKeyText, 10}};
const KeyDefinition kSymbols2[] = {
    {"ABC", "", kPokedexKeySymbols, 15}, {".", ".", kPokedexKeyText, 10},
    {",", ",", kPokedexKeyText, 10}, {"?", "?", kPokedexKeyText, 10},
    {"!", "!", kPokedexKeyText, 10}, {"'", "'", kPokedexKeyText, 10},
    {"\"", "\"", kPokedexKeyText, 10}, {"_", "_", kPokedexKeyText, 10},
    {"BACK", "", kPokedexKeyBackspace, 15}};
const KeyDefinition kSymbols3[] = {
    {"ABC", "", kPokedexKeySymbols, 14}, {"[", "[", kPokedexKeyText, 8},
    {"SPACE", " ", kPokedexKeyText, 42}, {"]", "]", kPokedexKeyText, 8},
    {"LEFT", "", kPokedexKeyLeft, 10}, {"RIGHT", "", kPokedexKeyRight, 10},
    {"RETURN", "", kPokedexKeyEnter, 18}};

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

void PokedexTouchKeyboard::reset() {
  shifted_ = false;
  symbols_ = false;
}

bool PokedexTouchKeyboard::shifted() const { return shifted_; }
bool PokedexTouchKeyboard::symbols() const { return symbols_; }

void PokedexTouchKeyboard::applyModeAction(PokedexKeyAction action) {
  if (action == kPokedexKeyShift) shifted_ = !shifted_;
  if (action == kPokedexKeySymbols) {
    symbols_ = !symbols_;
    shifted_ = false;
  }
}

PokedexKeyEvent PokedexTouchKeyboard::hitTest(int16_t x, int16_t y) const {
  PokedexKeyEvent event;
  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      int16_t keyX, keyY, keyW, keyH;
      keyBounds(row, rowIndex, keyIndex, keyX, keyY, keyW, keyH);
      if (!inside(x, y, keyX - kKeyGap / 2, keyY - kRowGap / 2,
                  keyW + kKeyGap, keyH + kRowGap)) continue;
      event.action = row.keys[keyIndex].action;
      event.text = row.keys[keyIndex].text;
      if (event.action == kPokedexKeyText && shifted_ && event.text.length() == 1 &&
          isalpha(static_cast<unsigned char>(event.text[0]))) {
        event.text[0] = toupper(event.text[0]);
      }
      return event;
    }
  }
  return event;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
void PokedexTouchKeyboard::draw(Arduino_GFX *g) const {
  if (g == nullptr) return;
  const uint16_t keyboardBg = Widgets::rgb(0x0B, 0x11, 0x1C);
  const uint16_t shadow = Widgets::rgb(0x05, 0x08, 0x10);
  const uint16_t panel = Widgets::rgb(0x16, 0x20, 0x2F);
  const uint16_t active = Widgets::rgb(0xFF, 0x54, 0x70);
  const uint16_t ink = Widgets::rgb(0xEA, 0xF0, 0xF7);
  const uint16_t rows[] = {
      Widgets::rgb(0x1E, 0x2B, 0x3D), Widgets::rgb(0x24, 0x3C, 0x50),
      Widgets::rgb(0x2C, 0x3A, 0x4F), Widgets::rgb(0x2F, 0x4A, 0x4D)};
  g->fillRect(0, kKeyboardY - 12, 1024, 296, keyboardBg);
  g->fillRoundRect(430, kKeyboardY - 8, 164, 5, 3, Widgets::rgb(0x16, 0xC2, 0xC9));

  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      const KeyDefinition &key = row.keys[keyIndex];
      int16_t x, y, w, h;
      keyBounds(row, rowIndex, keyIndex, x, y, w, h);
      bool modeActive = (key.action == kPokedexKeyShift && shifted_) ||
                        (key.action == kPokedexKeySymbols && symbols_);
      uint16_t fill = modeActive ? active :
                      (key.action == kPokedexKeyText ? rows[rowIndex] : panel);
      Widgets::panel(g, x + 3, y + 4, w, h, 11, shadow);
      Widgets::panel(g, x, y, w, h, 11, fill, modeActive ? 3 : 1,
                     modeActive ? active : Widgets::rgb(0x2A, 0x3A, 0x4F));
      const uint8_t *font = strlen(key.label) <= 2 ? PokedexText::fontM() : PokedexText::fontS();
      PokedexText::draw(g, x + w / 2, y + (strlen(key.label) <= 2 ? 17 : 21), key.label,
                        font, ink, PokedexText::kCenter);
    }
  }
}
#endif
