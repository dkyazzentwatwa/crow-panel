#ifndef CYPHER_DESK_PANEL_TOUCH_KEYBOARD_H
#define CYPHER_DESK_PANEL_TOUCH_KEYBOARD_H

#include "../config/ProjectConfig.h"
#include "DeskTouch.h"
#include "DeskTypes.h"
#include <Arduino.h>

// The on-screen keyboard, rebuilt on project 21's model.
//
// What changed from the tap-a-key version this project shipped with:
//
//   - A press is resolved at touch-DOWN against the contact's down position,
//     so sliding a finger off a key can no longer fire a different one.
//   - The pressed key is drawn and flushed immediately, on its own, before
//     anything else happens. That sub-frame region flush is the whole
//     difference between "a keyboard" and "this keyboard".
//   - Five contacts are tracked, so SHIFT can be HELD with one finger while
//     another types, and five keys can be lit at once.
//   - SHIFT is held-or-sticky: hold it and type for a real chord; tap it once
//     to arm a one-shot; tap it twice to disarm.
//   - Backspace and the arrows hold-repeat. Letters and Return deliberately
//     never do.
//
// The key TABLES stay this project's own: arrows, RETURN and writing
// punctuation, not project 21's CMD/OPT/CTRL row, which encodes HID semantics
// that mean nothing in an editor.

enum DeskKeyAction {
  kDeskKeyNone,
  kDeskKeyText,
  kDeskKeyBackspace,
  kDeskKeyEnter,
  kDeskKeyShift,
  kDeskKeySymbols,
  kDeskKeyLeft,
  kDeskKeyRight
};

struct DeskKeyEvent {
  DeskKeyAction action = kDeskKeyNone;
  String text;
};

class DeskAudioService;

class DeskTouchKeyboard {
 public:
  static constexpr uint8_t kEventQueue = 8;

  void reset();
  bool shifted() const;  // true when the next character will be upper case
  bool symbols() const;

  // Drives the keyboard from the shared touch tracker: binds presses, draws
  // and flushes press art, services hold-repeat, and queues events for the
  // caller to drain. `g` may be null in a headless build.
  void service(DeskTouch &touch, class Arduino_GFX *g, const DeskThemePalette &theme,
               DeskAudioService *audio);

  // Drains one queued event. Returns false when the queue is empty.
  bool nextEvent(DeskKeyEvent &out);

  // True when a modifier or layer change means the whole keyboard needs a
  // repaint. Clears on read.
  bool consumeRedraw();

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw(class Arduino_GFX *g, const DeskThemePalette &theme) const;
#endif

 private:
  struct PressArt {
    bool active = false;
    int16_t x = 0, y = 0, w = 0, h = 0;
  };

  bool shifted_ = false;    // one-shot, armed by a tap
  bool heldShift_ = false;  // a finger is resting on SHIFT right now
  bool shiftUsed_ = false;  // that held SHIFT already modified a key
  bool symbols_ = false;
  bool redraw_ = false;

  DeskKeyEvent queue_[kEventQueue];
  uint8_t queueHead_ = 0;
  uint8_t queueCount_ = 0;
  PressArt pressArt_[DeskTouch::kMaxContacts];

  bool push(const DeskKeyEvent &event);
  // Resolves a press. Returns the event to queue (action kDeskKeyNone for a
  // modifier or a miss) and writes the bound key id.
  DeskKeyEvent pressAt(int16_t x, int16_t y, uint8_t &keyIdOut);
  DeskKeyEvent repeatKey(uint8_t keyId);
  void releaseKey(uint8_t keyId);
  bool keyRepeats(uint8_t keyId) const;
  bool keyRect(uint8_t keyId, int16_t &x, int16_t &y, int16_t &w, int16_t &h) const;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void drawKeyById(class Arduino_GFX *g, const DeskThemePalette &theme, uint8_t keyId,
                   bool pressed) const;
#endif
};

#endif
