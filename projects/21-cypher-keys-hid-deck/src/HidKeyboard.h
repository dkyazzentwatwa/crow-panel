#ifndef CYPHER_KEYS_HID_KEYBOARD_H
#define CYPHER_KEYS_HID_KEYBOARD_H

#include "../config/ProjectConfig.h"
#include "DeckThemes.h"
#include "HidTypes.h"
#include <Arduino.h>

// On-screen QWERTY forked from Cypher Desk's DeskTouchKeyboard: same weighted
// 4-row geometry and hit-testing, but each key now carries a USB HID target
// instead of editor text, and a Mac modifier row (Ctrl/Opt/Cmd) turns it into a
// real keyboard.
//
// The key model is multi-touch and press-owned:
//
//  - A press is resolved at touch-DOWN (pressAt) and the caller binds the
//    returned key id to that contact, so sliding a finger off a key can never
//    fire a different one.
//  - Modifiers work two ways at once. HELD: keep a finger on CMD and tap C with
//    another -> Cmd+C, a real chord. STICKY: a quick tap of CMD that never
//    modified anything arms it one-shot for the next key, which is how this
//    keyboard always behaved.
//  - Backspace and the arrows auto-repeat while held (repeats/repeatKey).

// A resolved keypress the caller sends through HidBackend::tapKey.
struct HidKeyEvent {
  bool send = false;    // true when a key should be transmitted
  bool redraw = false;  // true when visible keyboard state changed (mod/shift/layer)
  uint8_t key = 0;      // ASCII or kKey* constant
  uint8_t mods = 0;     // modifiers to apply with this key (held | sticky)
};

class HidKeyboard {
 public:
  void reset();
  bool shifted() const { return shifted_; }
  bool symbols() const { return symbols_; }
  uint8_t stickyMods() const { return stickyMods_; }
  uint8_t heldMods() const { return heldMods_; }
  bool heldShift() const { return heldShift_; }
  // Modifiers that would ride along with a key pressed right now.
  uint8_t effectiveMods() const { return (uint8_t)(stickyMods_ | heldMods_); }

  // Key ids. A key is identified by `row * 16 + keyIndex` WITHIN THE LAYER that
  // was active when pressAt() resolved it (rows 0..3, at most 10 keys per row,
  // so ids run 0..58; -1 means "no key"). Ids are therefore layer-relative: the
  // 123/ABC key toggling the layer invalidates every outstanding id, so pressAt
  // drops all held modifiers on a layer toggle and the caller must clear the
  // owner of every other live contact (see HidDeck::serviceKeyboardTouch).
  static const int16_t kNoKey = -1;

  // Touch-DOWN at (x,y). Returns what to do now and reports which key was hit
  // so the caller can bind it to the contact and drive repeat/press visuals.
  HidKeyEvent pressAt(int16_t x, int16_t y, int16_t &keyIdOut);

  // Touch-UP for a key previously returned by pressAt (keyId from that call).
  HidKeyEvent releaseKey(int16_t keyId);

  // True if that key auto-repeats while held (Backspace and the arrows only).
  bool repeats(int16_t keyId) const;

  // Sound classification for a key the caller already resolved, so an SD sound
  // pack can play its per-row and per-special-key clips. Neither touches the
  // state machine.
  //
  //  keySoundClass - a KeySoundPacks::KeyClass value (which KeyAudio::KeyClass
  //                  aliases): Backspace / Enter / Space, else generic. Resolved
  //                  against the LIVE layer, like every other keyId lookup.
  //  keySoundRow   - the keyboard row 0..3, straight out of the packed id.
  uint8_t keySoundClass(int16_t keyId) const;
  uint8_t keySoundRow(int16_t keyId) const;

  // Build the repeat event for a key that is still held down.
  HidKeyEvent repeatKey(int16_t keyId);

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g, const DeckTheme &theme) const;

  // Find the on-screen rectangle of the key under (x,y) without mutating state.
  // Used for instant press feedback and single-key redraws. Returns false if
  // the point is not over a key.
  bool keyRectAt(int16_t x, int16_t y, int16_t &kx, int16_t &ky, int16_t &kw,
                 int16_t &kh) const;

  // Redraw just the key at the given rectangle. `pressed` draws the momentary
  // touch-down highlight; otherwise the key renders in its normal state.
  void drawSingleKey(class Arduino_GFX *g, const DeckTheme &theme, int16_t kx,
                     int16_t ky, int16_t kw, int16_t kh, bool pressed) const;
#endif

 private:
  // A key press consumes the one-shots and marks whatever is physically held as
  // "used", so releasing it does not also arm it sticky.
  void consumeOneShots();

  bool shifted_ = false;      // one-shot shift armed by a SHIFT tap
  bool symbols_ = false;      // symbols layer up
  uint8_t stickyMods_ = 0;    // one-shot modifiers armed by taps
  uint8_t heldMods_ = 0;      // modifiers a finger is physically holding
  bool heldShift_ = false;    // SHIFT is physically held
  uint8_t usedMods_ = 0;      // held modifiers that already modified a key
  bool shiftUsed_ = false;    // held SHIFT already shifted a key
};

#endif
