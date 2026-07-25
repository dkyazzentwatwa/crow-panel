#include "HidKeyboard.h"

#include "KeySoundPacks.h"  // KeyClass numbering shared with the audio engine
#include "KeysLayout.h"     // row/key rectangles and their touch targets

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#endif

namespace {

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

// Key ids pack the row and the index within it: row * 16 + keyIndex. 16 is a
// clean shift and every row has at most 10 keys, so the two never collide.
const uint8_t kKeysPerRowStride = 16;

int16_t makeKeyId(uint8_t rowIndex, uint8_t keyIndex) {
  return (int16_t)(rowIndex * kKeysPerRowStride + keyIndex);
}

// Resolve a key id against a layer. Returns nullptr when the id names no key in
// that layer (which is what a stale id from the other layer looks like).
const KeyDefinition *keyFromId(bool symbols, int16_t keyId) {
  if (keyId < 0) return nullptr;
  uint8_t rowIndex = (uint8_t)(keyId / kKeysPerRowStride);
  uint8_t keyIndex = (uint8_t)(keyId % kKeysPerRowStride);
  if (rowIndex >= 4) return nullptr;
  KeyboardRow row = rowAt(symbols, rowIndex);
  if (keyIndex >= row.count) return nullptr;
  return &row.keys[keyIndex];
}

// Locate the key under (px,py) using the half-gutter touch targets, and hand
// back both its indices and its rectangle. Pure integer math from KeysLayout,
// so this works in headless builds too.
bool locateKey(bool symbols, int16_t px, int16_t py, uint8_t &rowOut,
               uint8_t &keyOut, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      KeysLayout::keyBounds(row.keys, row.count, rowIndex, keyIndex, x, y, w, h);
      if (KeysLayout::hitKeyCell(px, py, x, y, w, h)) {
        rowOut = rowIndex;
        keyOut = keyIndex;
        return true;
      }
    }
  }
  return false;
}

// Backspace and the arrows are the only keys worth auto-repeating: they are the
// ones a user holds down. Repeating a letter or Return would be a footgun.
bool keyRepeats(const KeyDefinition *key) {
  if (key == nullptr || key->role != kRoleSpecial) return false;
  return key->key == kKeyBackspace || key->key == kKeyLeftArrow ||
         key->key == kKeyRightArrow || key->key == kKeyUpArrow ||
         key->key == kKeyDownArrow;
}

}  // namespace

void HidKeyboard::reset() {
  shifted_ = false;
  symbols_ = false;
  stickyMods_ = 0;
  heldMods_ = 0;
  heldShift_ = false;
  usedMods_ = 0;
  shiftUsed_ = false;
}

void HidKeyboard::consumeOneShots() {
  stickyMods_ = 0;
  shifted_ = false;
  // Anything still under a finger just did its job, so lifting it must not also
  // arm it sticky.
  usedMods_ |= heldMods_;
  if (heldShift_) shiftUsed_ = true;
}

HidKeyEvent HidKeyboard::pressAt(int16_t x, int16_t y, int16_t &keyIdOut) {
  HidKeyEvent event;
  keyIdOut = kNoKey;

  uint8_t rowIndex = 0, keyIndex = 0;
  int16_t kx = 0, ky = 0, kw = 0, kh = 0;  // rect unused here; keyRectAt serves it
  if (!locateKey(symbols_, x, y, rowIndex, keyIndex, kx, ky, kw, kh)) {
    return event;
  }
  keyIdOut = makeKeyId(rowIndex, keyIndex);
  const KeyDefinition &key = rowAt(symbols_, rowIndex).keys[keyIndex];

  switch (key.role) {
    case kRoleShift:
      heldShift_ = true;
      shiftUsed_ = false;  // not yet used; a quick tap will arm it sticky
      event.redraw = true;
      return event;
    case kRoleSymbols:
      symbols_ = !symbols_;
      shifted_ = false;
      // Key ids are layer-relative, so nothing held can be matched to its
      // release any more. Drop the chord rather than leave a stuck modifier.
      heldMods_ = 0;
      heldShift_ = false;
      usedMods_ = 0;
      shiftUsed_ = false;
      event.redraw = true;
      return event;
    case kRoleMod:
      heldMods_ |= key.mod;
      usedMods_ &= (uint8_t)~key.mod;  // not yet used
      event.redraw = true;
      return event;
    case kRoleSpecial:
      event.send = true;
      event.key = key.key;
      event.mods = effectiveMods();
      consumeOneShots();
      return event;
    case kRoleText:
    default: {
      uint8_t ascii = key.key;
      // Shift is expressed by the ASCII case (the HID layer derives the Shift
      // modifier from it), not by a kModShift bit in `mods`.
      if ((shifted_ || heldShift_) && ascii >= 'a' && ascii <= 'z') ascii -= 32;
      event.send = true;
      event.key = ascii;
      event.mods = effectiveMods();
      consumeOneShots();
      return event;
    }
  }
}

HidKeyEvent HidKeyboard::releaseKey(int16_t keyId) {
  HidKeyEvent event;
  const KeyDefinition *key = keyFromId(symbols_, keyId);
  if (key == nullptr) return event;

  switch (key->role) {
    case kRoleShift:
      heldShift_ = false;
      // A hold that shifted something is done; a hold that shifted nothing was
      // really a tap, so arm the classic one-shot shift.
      if (!shiftUsed_) shifted_ = !shifted_;
      shiftUsed_ = false;
      event.redraw = true;
      return event;
    case kRoleMod: {
      bool used = (usedMods_ & key->mod) != 0;
      heldMods_ &= (uint8_t)~key->mod;
      usedMods_ &= (uint8_t)~key->mod;
      if (!used) stickyMods_ ^= key->mod;  // quick tap -> sticky one-shot
      event.redraw = true;
      return event;
    }
    case kRoleSymbols:
    case kRoleSpecial:
    case kRoleText:
    default:
      // Nothing to send and no visible state change: the press already did all
      // the work. The caller just restores that key's normal art.
      return event;
  }
}

bool HidKeyboard::repeats(int16_t keyId) const {
  return keyRepeats(keyFromId(symbols_, keyId));
}

// Read-only classification for the sound engine. Classified by what the key
// actually sends, so BACK / RETURN / SPACE are recognized in both layers without
// a second table to keep in sync.
uint8_t HidKeyboard::keySoundClass(int16_t keyId) const {
  const KeyDefinition *key = keyFromId(symbols_, keyId);
  if (key == nullptr) return KeySoundPacks::kClassGeneric;
  if (key->role == kRoleSpecial) {
    if (key->key == kKeyBackspace) return KeySoundPacks::kClassBackspace;
    if (key->key == kKeyReturn) return KeySoundPacks::kClassEnter;
  } else if (key->role == kRoleText && key->key == ' ') {
    return KeySoundPacks::kClassSpace;
  }
  return KeySoundPacks::kClassGeneric;
}

uint8_t HidKeyboard::keySoundRow(int16_t keyId) const {
  if (keyId < 0) return 0;
  const uint8_t row = (uint8_t)(keyId / kKeysPerRowStride);
  if (row >= 4) return 0;
  // Translate this panel's 4 rows into the sample-pack row convention, whose
  // GENERIC_R<n> clips are pitch-adjusted per row of a full ANSI board counted
  // from the top: R0 = function row, R1 = numbers, R2 = QWERTY, R3 = ASDF,
  // R4 = ZXCV and everything below it (kbsim clamps its bottom rows to R4).
  // Verified against kbsim's KeySimulator.js row switch + its default KLE
  // preset, whose row 0 is "Esc F1..F12".
  //   letters layer: QWERTY/ASDF/ZXCV/modifiers -> R2, R3, R4, R4
  //   symbols layer: numbers sit in row 0        -> R1, R2, R3, R4
  // Picking the wrong row here is not a bug you can see, only hear: the click
  // would carry a neighbouring row's pitch.
  if (symbols_) {
    return (uint8_t)(row + 1);
  }
  const uint8_t mapped = (uint8_t)(row + 2);
  return mapped > 4 ? 4 : mapped;
}

HidKeyEvent HidKeyboard::repeatKey(int16_t keyId) {
  HidKeyEvent event;
  const KeyDefinition *key = keyFromId(symbols_, keyId);
  if (!keyRepeats(key)) return event;
  event.send = true;
  event.key = key->key;
  // The one-shots were consumed by the initial press; whatever is still held
  // rides along with every repeat.
  event.mods = effectiveMods();
  return event;
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {
void drawSmallCentered(Arduino_GFX *g, int16_t centerX, int16_t topY,
                       const char *label, uint16_t color) {
  Widgets::text(g, centerX, topY, label, Widgets::fontS(), color,
                Widgets::kCenter);
}

// A key draws highlighted when it is a held modifier, an armed sticky
// modifier, a held or armed shift, or the symbols-layer toggle while that layer
// is up - so chording lights up exactly like the old sticky taps did.
bool keyActive(const KeyDefinition &key, bool shiftActive, bool symbols,
               uint8_t modsActive) {
  if (key.role == kRoleShift) return shiftActive;
  if (key.role == kRoleSymbols) return symbols;
  if (key.role == kRoleMod) return (modsActive & key.mod) != 0;
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
}  // namespace

void HidKeyboard::draw(Arduino_GFX *g, const DeckTheme &theme) const {
  if (g == nullptr) return;
  g->fillRect(0, KeysLayout::kKeyboardBandY, KeysLayout::kScreenW,
              KeysLayout::kKeyboardBandH, theme.bg);
  g->fillRoundRect(KeysLayout::kKeyboardHandleX, KeysLayout::kKeyboardHandleY,
                   KeysLayout::kKeyboardHandleW, KeysLayout::kKeyboardHandleH, 3,
                   theme.accent);

  for (uint8_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
    KeyboardRow row = rowAt(symbols_, rowIndex);
    for (uint8_t keyIndex = 0; keyIndex < row.count; ++keyIndex) {
      const KeyDefinition &key = row.keys[keyIndex];
      int16_t x, y, w, h;
      KeysLayout::keyBounds(row.keys, row.count, rowIndex, keyIndex, x, y, w, h);
      bool active = keyActive(key, shifted_ || heldShift_, symbols_, effectiveMods());
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
  bool active = keyActive(key, shifted_ || heldShift_, symbols_, effectiveMods());
  drawKeyCell(g, theme, key, r, x, y, w, h, active, pressed);
}
#endif
