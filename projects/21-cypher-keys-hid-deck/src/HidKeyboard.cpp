#include "HidKeyboard.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#endif

namespace {

// Geometry copied verbatim from Cypher Desk's DeskTouchKeyboard so the panel's
// bottom half feels identical.
constexpr int16_t kKeyboardY = 316;
constexpr int16_t kKeyboardMarginX = 22;
constexpr int16_t kKeyboardWidth = 980;
constexpr int16_t kRowHeight = 60;
constexpr int16_t kRowGap = 8;
constexpr int16_t kKeyGap = 6;

enum KeyRole : uint8_t {
  kRoleText,     // ASCII character; honors shift for letters
  kRoleSpecial,  // send `key` verbatim (Return, Tab, arrows, Esc, Backspace)
  kRoleShift,    // toggle one-shot shift
  kRoleMod,      // toggle a sticky modifier (`mod` = HidMod bit)
  kRoleSymbols,  // toggle the symbols layer
};

struct KeyDefinition {
  const char *label;
  uint8_t key;    // ASCII (text) or kKey* (special); 0 otherwise
  KeyRole role;
  uint8_t mod;    // kRoleMod: HidMod bit
  uint8_t weight;
};

const KeyDefinition kLetters0[] = {
    {"Q", 'q', kRoleText, 0, 10}, {"W", 'w', kRoleText, 0, 10},
    {"E", 'e', kRoleText, 0, 10}, {"R", 'r', kRoleText, 0, 10},
    {"T", 't', kRoleText, 0, 10}, {"Y", 'y', kRoleText, 0, 10},
    {"U", 'u', kRoleText, 0, 10}, {"I", 'i', kRoleText, 0, 10},
    {"O", 'o', kRoleText, 0, 10}, {"P", 'p', kRoleText, 0, 10}};
const KeyDefinition kLetters1[] = {
    {"A", 'a', kRoleText, 0, 10}, {"S", 's', kRoleText, 0, 10},
    {"D", 'd', kRoleText, 0, 10}, {"F", 'f', kRoleText, 0, 10},
    {"G", 'g', kRoleText, 0, 10}, {"H", 'h', kRoleText, 0, 10},
    {"J", 'j', kRoleText, 0, 10}, {"K", 'k', kRoleText, 0, 10},
    {"L", 'l', kRoleText, 0, 10}};
const KeyDefinition kLetters2[] = {
    {"SHIFT", 0, kRoleShift, 0, 15}, {"Z", 'z', kRoleText, 0, 10},
    {"X", 'x', kRoleText, 0, 10}, {"C", 'c', kRoleText, 0, 10},
    {"V", 'v', kRoleText, 0, 10}, {"B", 'b', kRoleText, 0, 10},
    {"N", 'n', kRoleText, 0, 10}, {"M", 'm', kRoleText, 0, 10},
    {"BACK", kKeyBackspace, kRoleSpecial, 0, 15}};
const KeyDefinition kLetters3[] = {
    {"123", 0, kRoleSymbols, 0, 14}, {"CTRL", 0, kRoleMod, kModCtrl, 12},
    {"OPT", 0, kRoleMod, kModOpt, 12}, {"CMD", 0, kRoleMod, kModCmd, 12},
    {"SPACE", ' ', kRoleText, 0, 34}, {"<", kKeyLeftArrow, kRoleSpecial, 0, 12},
    {">", kKeyRightArrow, kRoleSpecial, 0, 12},
    {"RETURN", kKeyReturn, kRoleSpecial, 0, 18}};

const KeyDefinition kSymbols0[] = {
    {"1", '1', kRoleText, 0, 10}, {"2", '2', kRoleText, 0, 10},
    {"3", '3', kRoleText, 0, 10}, {"4", '4', kRoleText, 0, 10},
    {"5", '5', kRoleText, 0, 10}, {"6", '6', kRoleText, 0, 10},
    {"7", '7', kRoleText, 0, 10}, {"8", '8', kRoleText, 0, 10},
    {"9", '9', kRoleText, 0, 10}, {"0", '0', kRoleText, 0, 10}};
const KeyDefinition kSymbols1[] = {
    {"-", '-', kRoleText, 0, 10}, {"/", '/', kRoleText, 0, 10},
    {":", ':', kRoleText, 0, 10}, {";", ';', kRoleText, 0, 10},
    {"(", '(', kRoleText, 0, 10}, {")", ')', kRoleText, 0, 10},
    {"$", '$', kRoleText, 0, 10}, {"&", '&', kRoleText, 0, 10},
    {"@", '@', kRoleText, 0, 10}, {"#", '#', kRoleText, 0, 10}};
const KeyDefinition kSymbols2[] = {
    {"ABC", 0, kRoleSymbols, 0, 15}, {".", '.', kRoleText, 0, 10},
    {",", ',', kRoleText, 0, 10}, {"?", '?', kRoleText, 0, 10},
    {"!", '!', kRoleText, 0, 10}, {"'", '\'', kRoleText, 0, 10},
    {"\"", '"', kRoleText, 0, 10}, {"_", '_', kRoleText, 0, 10},
    {"BACK", kKeyBackspace, kRoleSpecial, 0, 15}};
const KeyDefinition kSymbols3[] = {
    {"ABC", 0, kRoleSymbols, 0, 14}, {"ESC", kKeyEsc, kRoleSpecial, 0, 12},
    {"SPACE", ' ', kRoleText, 0, 34}, {"TAB", kKeyTab, kRoleSpecial, 0, 12},
    {"<", kKeyLeftArrow, kRoleSpecial, 0, 12},
    {">", kKeyRightArrow, kRoleSpecial, 0, 12},
    {"RETURN", kKeyReturn, kRoleSpecial, 0, 18}};

struct KeyboardRow {
  const KeyDefinition *keys;
  uint8_t count;
};

KeyboardRow rowAt(bool symbols, uint8_t row) {
  if (!symbols) {
    if (row == 0) return {kLetters0, 10};
    if (row == 1) return {kLetters1, 9};
    if (row == 2) return {kLetters2, 9};
    return {kLetters3, 8};
  }
  if (row == 0) return {kSymbols0, 10};
  if (row == 1) return {kSymbols1, 10};
  if (row == 2) return {kSymbols2, 9};
  return {kSymbols3, 7};
}

int16_t rowInset(uint8_t row) { return row == 1 ? 44 : 0; }
int16_t rowWidth(uint8_t row) { return kKeyboardWidth - rowInset(row) * 2; }

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

void HidKeyboard::reset() {
  shifted_ = false;
  symbols_ = false;
  stickyMods_ = 0;
}

HidKeyEvent HidKeyboard::hitTest(int16_t x, int16_t y) {
  HidKeyEvent event;
  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      int16_t keyX, keyY, keyW, keyH;
      keyBounds(row, rowIndex, keyIndex, keyX, keyY, keyW, keyH);
      // Extend each target into half the gutter (matches Desk) so there are no
      // dead strips between keys.
      if (!inside(x, y, keyX - kKeyGap / 2, keyY - kRowGap / 2, keyW + kKeyGap,
                  keyH + kRowGap))
        continue;

      const KeyDefinition &key = row.keys[keyIndex];
      switch (key.role) {
        case kRoleShift:
          shifted_ = !shifted_;
          event.redraw = true;
          return event;
        case kRoleSymbols:
          symbols_ = !symbols_;
          shifted_ = false;
          event.redraw = true;
          return event;
        case kRoleMod:
          stickyMods_ ^= key.mod;  // toggle that modifier
          event.redraw = true;
          return event;
        case kRoleSpecial:
          event.send = true;
          event.key = key.key;
          event.mods = stickyMods_;
          stickyMods_ = 0;  // one-shot
          return event;
        case kRoleText:
        default: {
          uint8_t ascii = key.key;
          if (shifted_ && ascii >= 'a' && ascii <= 'z') ascii -= 32;
          event.send = true;
          event.key = ascii;
          event.mods = stickyMods_;
          stickyMods_ = 0;    // one-shot modifiers
          shifted_ = false;   // one-shot shift
          return event;
        }
      }
    }
  }
  return event;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {
void drawSmallCentered(Arduino_GFX *g, int16_t centerX, int16_t topY,
                       const char *label, uint16_t color) {
  Widgets::text(g, centerX, topY, label, Widgets::fontS(), color,
                Widgets::kCenter);
}

bool keyActive(const KeyDefinition &key, bool shifted, bool symbols,
               uint8_t sticky) {
  if (key.role == kRoleShift) return shifted;
  if (key.role == kRoleSymbols) return symbols;
  if (key.role == kRoleMod) return (sticky & key.mod) != 0;
  return false;
}

// Render one key cell. `pressed` is the momentary touch-down highlight; it and
// the sticky "active" state both use the accent fill so feedback is obvious.
void drawKeyCell(Arduino_GFX *g, const DeckTheme &theme, const KeyDefinition &key,
                 uint8_t rowIndex, int16_t x, int16_t y, int16_t w, int16_t h,
                 bool active, bool pressed) {
  const uint16_t rowFill[4] = {theme.keyFill, theme.keyFillAlt, theme.keyFill,
                               theme.keyFillAlt};
  bool hot = active || pressed;
  bool isAction = key.role != kRoleText;
  uint16_t fill = hot ? theme.accent
                      : (isAction ? theme.surfaceHi : rowFill[rowIndex]);
  uint16_t ink = hot ? theme.onAccent : theme.ink;
  Widgets::panel(g, x + 3, y + 4, w, h, 11, theme.bg);
  Widgets::panel(g, x, y, w, h, 11, fill, hot ? 3 : 1, theme.line);
  if (strlen(key.label) <= 2) {
    Widgets::text(g, x + w / 2, y + 20, key.label, Widgets::fontM(), ink,
                  Widgets::kCenter);
  } else {
    drawSmallCentered(g, x + w / 2, y + 24, key.label, ink);
  }
}

// Locate the key under (px,py) using the same half-gutter targets as hitTest.
bool locateKey(bool symbols, int16_t px, int16_t py, uint8_t &rowOut,
               uint8_t &keyOut, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      keyBounds(row, rowIndex, keyIndex, x, y, w, h);
      if (inside(px, py, x - kKeyGap / 2, y - kRowGap / 2, w + kKeyGap,
                 h + kRowGap)) {
        rowOut = rowIndex;
        keyOut = keyIndex;
        return true;
      }
    }
  }
  return false;
}
}  // namespace

void HidKeyboard::draw(Arduino_GFX *g, const DeckTheme &theme) const {
  if (g == nullptr) return;
  g->fillRect(0, kKeyboardY - 12, 1024, 296, theme.bg);
  g->fillRoundRect(430, kKeyboardY - 8, 164, 5, 3, theme.accent);

  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      const KeyDefinition &key = row.keys[keyIndex];
      int16_t x, y, w, h;
      keyBounds(row, rowIndex, keyIndex, x, y, w, h);
      bool active = keyActive(key, shifted_, symbols_, stickyMods_);
      drawKeyCell(g, theme, key, rowIndex, x, y, w, h, active, false);
    }
  }
}

bool HidKeyboard::keyRectAt(int16_t x, int16_t y, int16_t &kx, int16_t &ky,
                            int16_t &kw, int16_t &kh) const {
  uint8_t r, k;
  return locateKey(symbols_, x, y, r, k, kx, ky, kw, kh);
}

void HidKeyboard::drawSingleKey(Arduino_GFX *g, const DeckTheme &theme,
                                int16_t kx, int16_t ky, int16_t kw, int16_t kh,
                                bool pressed) const {
  if (g == nullptr) return;
  uint8_t r, k;
  int16_t x, y, w, h;
  if (!locateKey(symbols_, kx + kw / 2, ky + kh / 2, r, k, x, y, w, h)) return;
  const KeyDefinition &key = rowAt(symbols_, r).keys[k];
  bool active = keyActive(key, shifted_, symbols_, stickyMods_);
  drawKeyCell(g, theme, key, r, x, y, w, h, active, pressed);
}
#endif
