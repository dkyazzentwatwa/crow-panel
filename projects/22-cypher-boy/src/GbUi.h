#ifndef CYPHER_BOY_UI_H
#define CYPHER_BOY_UI_H

#include "../config/ProjectConfig.h"
#include "GbRomStore.h"
#include "GbTheme.h"
#include <Arduino.h>

// Draws the two screens: the ROM picker and the in-game chrome (the gamepad
// overlay drawn around the live viewport). Rendering only - it never mutates
// app state; the .ino owns the screen enum and the transitions.
//
// The picker row maths is deliberately kept out of the display guard so the
// selftest can verify hit-testing headlessly.
class GbUi {
 public:
  static const int16_t kRowH = 64;
  static const int16_t kRowTop = 110;   // first row's top edge
  static const int16_t kRowX = 40;
  static const int16_t kRowW = 620;

  // In-game pause menu actions (Delta-style). MENU opens the overlay rather
  // than quitting outright, so a stray tap can't dump you out of a game.
  enum OverlayAction : uint8_t {
    kOvNone = 0,
    kOvResume,
    kOvSaveState,
    kOvLoadState,
    kOvToggleFF,
    kOvToggleSound,
    kOvQuit,
    kOvNextTheme,
  };

  // A settings row: label, minus button, level bar, plus button. Used on both
  // the picker and the pause overlay so sound/brightness feel the same in and
  // out of a game.
  static const int16_t kStepH = 68;
  enum StepHit : int8_t { kStepNone = 0, kStepMinus = -1, kStepPlus = 1 };
  static StepHit stepperHit(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w);

  // Settings block on the picker (volume + brightness), and its hit-testers.
  static const int16_t kSetX = 700, kSetY = 190, kSetW = 300;
  static int16_t volRowY() { return kSetY + 44; }
  static int16_t brightRowY() { return kSetY + 44 + kStepH + 12; }
  // THEME row under the two steppers on the picker's settings card.
  static int16_t themeRowY() { return kSetY + 44 + (kStepH + 12) * 2; }
  static bool themeRowHit(int16_t px, int16_t py);

  // Active theme. Held statically inside GbUi rather than threaded through
  // every draw signature - it changes rarely and a repaint always follows.
  static void setTheme(GbThemeId id);
  static GbThemeId theme();
  static const GbPalette &palette();

  bool begin();
  // `order` maps display row -> ROM index (most recently played first), and
  // `played`/`isColor` are per-ROM display data. Pass nullptr for order to
  // show the ROMs in card order.
  void drawPicker(const GbRomStore &roms, int8_t selectedRow,
                  const uint8_t *order = nullptr,
                  const String *played = nullptr);
  void drawSettings(uint8_t volume, uint8_t brightness, bool muted);
  void drawPlayChrome(const String &title, bool soundOn, bool fastForward);
  void drawButtonState(uint32_t heldBits);  // light up pressed controls

  void drawPauseOverlay(uint8_t slot, bool soundOn, bool fastForward,
                        uint8_t volume, uint8_t brightness);
  // Where slot thumbnails come from. Set once at boot; the overlay reads it so
  // it can render each slot card without the .ino passing paths every frame.
  void setStateSource(const GbRomStore *roms, const int8_t *activeRom);
  // Which overlay control a release landed on. Pure maths, headless-safe.
  static OverlayAction overlayHit(int16_t px, int16_t py);
  static uint8_t overlaySlotHit(int16_t px, int16_t py);  // 0xFF if none
  // Volume / brightness steppers inside the overlay.
  static StepHit overlayVolHit(int16_t px, int16_t py);
  static StepHit overlayBrightHit(int16_t px, int16_t py);

  // Which ROM row a point lands on, or -1. Pure maths, headless-safe.
  static int8_t pickerHit(int16_t px, int16_t py, uint8_t romCount);

  // "Pokemon - Blue Version (USA, Europe) (SGB Enhanced).gb" -> "Pokemon - Blue Version"
  static String prettyTitle(const String &fileName);

  bool ready() const { return ready_; }

 private:
  void drawSlotThumb(class Arduino_GFX *g, int16_t x, int16_t y, uint8_t slot);

  bool ready_ = false;
  uint32_t lastHeld_ = 0;
  const GbRomStore *stateRoms_ = nullptr;
  const int8_t *stateRomIdx_ = nullptr;
};

#endif
