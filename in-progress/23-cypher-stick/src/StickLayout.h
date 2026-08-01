#ifndef CYPHER_STICK_STICK_LAYOUT_H
#define CYPHER_STICK_STICK_LAYOUT_H

#include <stdint.h>

#include "SocdCleaner.h"

// Key count / profile count are duplicated here rather than pulled from
// ProjectConfig.h so this header stays host-compilable. The static_asserts in
// StickEngine.cpp keep them agreeing with the project config.
#define STICK_LAYOUT_MAX_KEYS 20
#define STICK_LAYOUT_MAX_PROFILES 8

enum StickBind : uint8_t {
  // Directions feed the SOCD cleaner and become the hat.
  kBindUp = 0,
  kBindDown = 1,
  kBindLeft = 2,
  kBindRight = 3,
  // Anything >= kBindButton0 is a gamepad button index (bind - kBindButton0).
  kBindButton0 = 16,
  kBindNone = 255,
};

enum StickShape : uint8_t { kShapeRect = 0, kShapeRound = 1 };

struct StickKey {
  char label[8];
  int16_t x, y, w, h;
  uint8_t shape;
  uint16_t color;
  uint8_t bind;  // StickBind
  uint8_t key;   // keycode used in keyboard output mode
};

struct StickProfile {
  char name[16];
  StickKey keys[STICK_LAYOUT_MAX_KEYS];
  uint8_t keyCount;
  uint8_t socdPolicy;
};

// Resolved input state for one poll.
struct StickState {
  uint32_t buttons;  // bit N = gamepad button N held
  bool up, down, left, right;
};

// Index of the key containing (x, y), or -1 if none. Later keys win on overlap,
// so a key dragged on top of another in the editor takes the press.
int stickHitTest(const StickProfile &p, int16_t x, int16_t y);

// Fold a set of hit key indices into a StickState. Indices of -1 are ignored,
// which is how contacts landing outside every key (a resting palm) are dropped.
StickState stickResolve(const StickProfile &p, const int *hits, int hitCount);

// Fill `p` with the default 8-button leverless layout for a 1024x600 panel.
void stickDefaultProfile(StickProfile &p);

#endif
