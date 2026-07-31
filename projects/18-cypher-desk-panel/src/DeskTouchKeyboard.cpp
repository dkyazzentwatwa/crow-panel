#include "DeskTouchKeyboard.h"

#include "DeskKeyboardLayout.h"
#include "DeskSystemServices.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include <CrowPanelShared.h>
#include <U8g2lib.h>
#endif

using namespace DeskKeyboardLayout;

namespace {

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
// Markdown lives in this project's notes, so the symbols layer carries the
// characters a .md file actually needs: * for emphasis, # for headings (row 1),
// [ ] for links, > for quotes and ` for code.
const KeyDefinition kSymbols3[] = {
    {"ABC", "", kDeskKeySymbols, 14}, {"*", "*", kDeskKeyText, 8},
    {"SPACE", " ", kDeskKeyText, 34}, {"[", "[", kDeskKeyText, 8},
    {"]", "]", kDeskKeyText, 8}, {">", ">", kDeskKeyText, 8},
    {"`", "`", kDeskKeyText, 8},
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
  return {kSymbols3, 10};
}

// Resolves a point to a key cell, returning its row/index and rect.
bool locateKey(bool symbols, int16_t px, int16_t py, uint8_t &rowOut, uint8_t &indexOut,
               int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  for (uint8_t row = 0; row < kRows; ++row) {
    const KeyboardRow definition = rowAt(symbols, row);
    for (uint8_t index = 0; index < definition.count; ++index) {
      if (!keyBounds(definition.keys, definition.count, row, index, x, y, w, h)) continue;
      if (!hitKeyCell(px, py, x, y, w, h)) continue;
      rowOut = row;
      indexOut = index;
      return true;
    }
  }
  return false;
}

const KeyDefinition *keyFor(bool symbols, uint8_t keyId) {
  const uint8_t row = keyIdRow(keyId);
  const uint8_t index = keyIdIndex(keyId);
  if (row >= kRows) return nullptr;
  const KeyboardRow definition = rowAt(symbols, row);
  if (index >= definition.count) return nullptr;
  return &definition.keys[index];
}

}  // namespace

void DeskTouchKeyboard::reset() {
  shifted_ = false;
  heldShift_ = false;
  shiftUsed_ = false;
  symbols_ = false;
  queueHead_ = 0;
  queueCount_ = 0;
  redraw_ = true;
  for (uint8_t i = 0; i < DeskTouch::kMaxContacts; ++i) pressArt_[i].active = false;
}

bool DeskTouchKeyboard::shifted() const { return shifted_ || heldShift_; }
bool DeskTouchKeyboard::symbols() const { return symbols_; }

bool DeskTouchKeyboard::consumeRedraw() {
  const bool value = redraw_;
  redraw_ = false;
  return value;
}

bool DeskTouchKeyboard::push(const DeskKeyEvent &event) {
  if (event.action == kDeskKeyNone) return false;
  if (queueCount_ >= kEventQueue) return false;  // typing faster than draining
  queue_[(queueHead_ + queueCount_) % kEventQueue] = event;
  ++queueCount_;
  return true;
}

bool DeskTouchKeyboard::nextEvent(DeskKeyEvent &out) {
  if (queueCount_ == 0) return false;
  out = queue_[queueHead_];
  queueHead_ = (queueHead_ + 1) % kEventQueue;
  --queueCount_;
  return true;
}

bool DeskTouchKeyboard::keyRect(uint8_t keyId, int16_t &x, int16_t &y, int16_t &w,
                                int16_t &h) const {
  const uint8_t row = keyIdRow(keyId);
  const uint8_t index = keyIdIndex(keyId);
  if (row >= kRows) return false;
  const KeyboardRow definition = rowAt(symbols_, row);
  return keyBounds(definition.keys, definition.count, row, index, x, y, w, h);
}

// Only backspace and the arrows repeat. A letter or Return that repeated on a
// resting finger would be actively hostile in an editor.
bool DeskTouchKeyboard::keyRepeats(uint8_t keyId) const {
  const KeyDefinition *key = keyFor(symbols_, keyId);
  if (key == nullptr) return false;
  return key->action == kDeskKeyBackspace || key->action == kDeskKeyLeft ||
         key->action == kDeskKeyRight;
}

DeskKeyEvent DeskTouchKeyboard::pressAt(int16_t x, int16_t y, uint8_t &keyIdOut) {
  DeskKeyEvent event;
  keyIdOut = kNoKey;
  uint8_t row = 0;
  uint8_t index = 0;
  int16_t kx = 0, ky = 0, kw = 0, kh = 0;
  if (!locateKey(symbols_, x, y, row, index, kx, ky, kw, kh)) return event;

  keyIdOut = makeKeyId(row, index);
  const KeyDefinition &key = rowAt(symbols_, row).keys[index];

  if (key.action == kDeskKeyShift) {
    heldShift_ = true;
    shiftUsed_ = false;
    redraw_ = true;
    return event;
  }
  if (key.action == kDeskKeySymbols) {
    symbols_ = !symbols_;
    // Key ids are layer-relative, so every outstanding binding just became
    // meaningless. Drop the modifier state with them.
    shifted_ = false;
    heldShift_ = false;
    shiftUsed_ = false;
    redraw_ = true;
    return event;
  }

  event.action = key.action;
  event.text = key.text;
  if (event.action == kDeskKeyText && shifted() && event.text.length() == 1 &&
      isalpha(static_cast<unsigned char>(event.text[0]))) {
    event.text.setCharAt(0, toupper(event.text[0]));
  }
  // A held SHIFT that actually modified something has done its job, so its
  // release arms nothing. A one-shot is spent either way.
  if (heldShift_) shiftUsed_ = true;
  if (shifted_) {
    shifted_ = false;
    redraw_ = true;
  }
  return event;
}

DeskKeyEvent DeskTouchKeyboard::repeatKey(uint8_t keyId) {
  DeskKeyEvent event;
  const KeyDefinition *key = keyFor(symbols_, keyId);
  if (key == nullptr) return event;
  event.action = key->action;
  event.text = key->text;
  return event;
}

void DeskTouchKeyboard::releaseKey(uint8_t keyId) {
  const KeyDefinition *key = keyFor(symbols_, keyId);
  if (key == nullptr) return;
  if (key->action != kDeskKeyShift) return;
  heldShift_ = false;
  // A quick tap that modified nothing arms the one-shot; tapping again while
  // it is armed disarms it.
  if (!shiftUsed_) shifted_ = !shifted_;
  shiftUsed_ = false;
  redraw_ = true;
}

void DeskTouchKeyboard::service(DeskTouch &touch, Arduino_GFX *g, const DeskThemePalette &theme,
                                DeskAudioService *audio) {
  const uint32_t now = millis();
  const bool layerBefore = symbols_;

  for (uint8_t i = 0; i < DeskTouch::kMaxContacts; ++i) {
    DeskTouch::Contact &contact = touch.contact(i);

    if (contact.pressedEdge) {
      // Hit-test the DOWN position, not the live one: a finger that slides off
      // must never fire a neighbour.
      uint8_t keyId = kNoKey;
      const bool symbolsBefore = symbols_;
      const DeskKeyEvent event = pressAt(contact.downX, contact.downY, keyId);
      contact.owner = keyId == kNoKey ? -1 : static_cast<int16_t>(keyId);
      if (keyId != kNoKey) {
        push(event);
        if (audio != nullptr) audio->keyPress();
        contact.nextRepeatMs = keyRepeats(keyId) ? now + CYPHER_DESK_KEY_REPEAT_DELAY_MS : 0;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
        // Light the key and push just that rectangle. This runs before the
        // event is acted on, so the panel answers the finger immediately even
        // if the app then spends a frame reflowing text.
        //
        // Skipped when the press just toggled the layer: key ids are
        // layer-relative, so the rect would be resolved against the layout
        // that is on its way out, and a full repaint is coming anyway.
        int16_t kx, ky, kw, kh;
        if (g != nullptr && symbols_ == symbolsBefore && keyRect(keyId, kx, ky, kw, kh)) {
          drawKeyById(g, theme, keyId, /*pressed=*/true);
          CrowDisplay::flush(kx - 3, ky - 3, kw + 10, kh + 10);
          pressArt_[i] = {true, kx, ky, kw, kh};
        }
#else
        (void)symbolsBefore;
        (void)g;
        (void)theme;
#endif
      }
      continue;
    }

    if (contact.active && contact.owner >= 0 && contact.nextRepeatMs != 0 &&
        static_cast<int32_t>(now - contact.nextRepeatMs) >= 0) {
      push(repeatKey(static_cast<uint8_t>(contact.owner)));
      contact.nextRepeatMs = now + CYPHER_DESK_KEY_REPEAT_MS;
      continue;
    }

    if (contact.releasedEdge && contact.owner >= 0) {
      releaseKey(static_cast<uint8_t>(contact.owner));
      if (audio != nullptr) audio->keyRelease();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
      // Restore just this key unless a full repaint is already coming.
      if (g != nullptr && pressArt_[i].active && !redraw_ && symbols_ == layerBefore) {
        drawKeyById(g, theme, static_cast<uint8_t>(contact.owner), /*pressed=*/false);
        CrowDisplay::flush(pressArt_[i].x - 3, pressArt_[i].y - 3, pressArt_[i].w + 10,
                           pressArt_[i].h + 10);
      }
#endif
      pressArt_[i].active = false;
    }
  }
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

namespace {

void drawSmallCentered(Arduino_GFX *g, int16_t centerX, int16_t topY, const char *label,
                       uint16_t color) {
  g->setFont(u8g2_font_cubic11_h_cjk);
  g->setUTF8Print(true);
  g->setTextSize(1);
  g->setTextColor(color);
  int16_t bx, by;
  uint16_t bw, bh;
  g->getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  g->setCursor(centerX - static_cast<int16_t>(bw) / 2 - bx, topY - by);
  g->print(label);
}

// One key cap. The visual language is project 21's: an offset shadow panel
// behind the cap, alternating row fills, a border that thickens from 1 to 3 px
// when the key is hot, and a label font chosen by label length.
void drawKeyCell(Arduino_GFX *g, const DeskThemePalette &theme, const KeyDefinition &key,
                 uint8_t rowIndex, int16_t x, int16_t y, int16_t w, int16_t h, bool active,
                 bool pressed) {
  const uint16_t rowLines[] = {theme.accent, theme.accent3, theme.accent2, theme.success};
  const bool hot = active || pressed;
  const bool isAction = key.action != kDeskKeyText;
  const uint16_t fill = hot ? theme.accent
                            : (isAction ? theme.panelHighlight : theme.keyboardRows[rowIndex]);
  const uint16_t ink = hot ? theme.onAccent : theme.ink;
  Widgets::panel(g, x + 3, y + 4, w, h, 11, theme.background);
  Widgets::panel(g, x, y, w, h, 11, fill, hot ? 3 : 1, rowLines[rowIndex]);
  if (strlen(key.label) <= 2) {
    Widgets::text(g, x + w / 2, y + 17, key.label, Widgets::fontM(), ink, Widgets::kCenter);
  } else {
    drawSmallCentered(g, x + w / 2, y + 21, key.label, ink);
  }
}

}  // namespace

void DeskTouchKeyboard::drawKeyById(Arduino_GFX *g, const DeskThemePalette &theme, uint8_t keyId,
                                    bool pressed) const {
  const KeyDefinition *key = keyFor(symbols_, keyId);
  if (key == nullptr) return;
  int16_t x, y, w, h;
  if (!keyRect(keyId, x, y, w, h)) return;
  const bool active = (key->action == kDeskKeyShift && shifted()) ||
                      (key->action == kDeskKeySymbols && symbols_);
  drawKeyCell(g, theme, *key, keyIdRow(keyId), x, y, w, h, active, pressed);
}

void DeskTouchKeyboard::draw(Arduino_GFX *g, const DeskThemePalette &theme) const {
  if (g == nullptr) return;
  g->fillRect(0, kBandY, kScreenW, kBandH, theme.shell);
  g->fillRoundRect(kHandleX, kHandleY, kHandleW, kHandleH, 3, theme.accent);

  for (uint8_t row = 0; row < kRows; ++row) {
    const KeyboardRow definition = rowAt(symbols_, row);
    for (uint8_t index = 0; index < definition.count; ++index) {
      const KeyDefinition &key = definition.keys[index];
      int16_t x, y, w, h;
      if (!keyBounds(definition.keys, definition.count, row, index, x, y, w, h)) continue;
      const bool active = (key.action == kDeskKeyShift && shifted()) ||
                          (key.action == kDeskKeySymbols && symbols_);
      drawKeyCell(g, theme, key, row, x, y, w, h, active, /*pressed=*/false);
    }
  }
}
#endif
