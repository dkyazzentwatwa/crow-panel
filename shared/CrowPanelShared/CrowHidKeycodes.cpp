#include "CrowHidKeycodes.h"

uint8_t hidModifierByte(uint8_t mods) {
  uint8_t b = 0;
  if (mods & kModCtrl) b |= 0x01;   // Left Ctrl
  if (mods & kModShift) b |= 0x02;  // Left Shift
  if (mods & kModOpt) b |= 0x04;    // Left Alt/Option
  if (mods & kModCmd) b |= 0x08;    // Left GUI/Command
  return b;
}

bool hidUsageForKey(uint8_t key, uint8_t &usage, bool &needsShift) {
  needsShift = false;

  // Letters.
  if (key >= 'a' && key <= 'z') { usage = 0x04 + (key - 'a'); return true; }
  if (key >= 'A' && key <= 'Z') { usage = 0x04 + (key - 'A'); needsShift = true; return true; }

  // Digits (1-9 then 0).
  if (key >= '1' && key <= '9') { usage = 0x1E + (key - '1'); return true; }
  if (key == '0') { usage = 0x27; return true; }

  // Unshifted punctuation.
  switch (key) {
    case ' ': usage = 0x2C; return true;
    case '-': usage = 0x2D; return true;
    case '=': usage = 0x2E; return true;
    case '[': usage = 0x2F; return true;
    case ']': usage = 0x30; return true;
    case '\\': usage = 0x31; return true;
    case ';': usage = 0x33; return true;
    case '\'': usage = 0x34; return true;
    case '`': usage = 0x35; return true;
    case ',': usage = 0x36; return true;
    case '.': usage = 0x37; return true;
    case '/': usage = 0x38; return true;
  }

  // Shifted punctuation (base usage + Shift).
  switch (key) {
    case '!': usage = 0x1E; needsShift = true; return true;
    case '@': usage = 0x1F; needsShift = true; return true;
    case '#': usage = 0x20; needsShift = true; return true;
    case '$': usage = 0x21; needsShift = true; return true;
    case '%': usage = 0x22; needsShift = true; return true;
    case '^': usage = 0x23; needsShift = true; return true;
    case '&': usage = 0x24; needsShift = true; return true;
    case '*': usage = 0x25; needsShift = true; return true;
    case '(': usage = 0x26; needsShift = true; return true;
    case ')': usage = 0x27; needsShift = true; return true;
    case '_': usage = 0x2D; needsShift = true; return true;
    case '+': usage = 0x2E; needsShift = true; return true;
    case '{': usage = 0x2F; needsShift = true; return true;
    case '}': usage = 0x30; needsShift = true; return true;
    case '|': usage = 0x31; needsShift = true; return true;
    case ':': usage = 0x33; needsShift = true; return true;
    case '"': usage = 0x34; needsShift = true; return true;
    case '~': usage = 0x35; needsShift = true; return true;
    case '<': usage = 0x36; needsShift = true; return true;
    case '>': usage = 0x37; needsShift = true; return true;
    case '?': usage = 0x38; needsShift = true; return true;
  }

  // Special keys (kKey* / Arduino KEY_* constants from CrowHidTypes.h).
  switch (key) {
    case kKeyReturn: usage = 0x28; return true;
    case kKeyEsc: usage = 0x29; return true;
    case kKeyBackspace: usage = 0x2A; return true;
    case kKeyTab: usage = 0x2B; return true;
    case kKeyRightArrow: usage = 0x4F; return true;
    case kKeyLeftArrow: usage = 0x50; return true;
    case kKeyDownArrow: usage = 0x51; return true;
    case kKeyUpArrow: usage = 0x52; return true;
    case kKeyF1: usage = 0x3A; return true;
    case kKeyF2: usage = 0x3B; return true;
    case kKeyF3: usage = 0x3C; return true;
    case kKeyF4: usage = 0x3D; return true;
    case kKeyF5: usage = 0x3E; return true;
    case kKeyF6: usage = 0x3F; return true;
    case kKeyF7: usage = 0x40; return true;
    case kKeyF8: usage = 0x41; return true;
    case kKeyF9: usage = 0x42; return true;
    case kKeyF10: usage = 0x43; return true;
    case kKeyF11: usage = 0x44; return true;
    case kKeyF12: usage = 0x45; return true;
  }
  return false;
}
