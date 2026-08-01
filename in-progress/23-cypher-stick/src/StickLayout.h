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
  // 4-15 are reserved (the gap between the last direction and the first
  // button) and are treated as unbound, the same as an explicit kBindNone.
  // Anything >= kBindButton0 is a gamepad button index (bind - kBindButton0).
  kBindButton0 = 16,
  kBindNone = 255,
};

// Shared bind decode. Every site that needs to know what a bind byte means
// (hit resolution here, the editor's bind cycler, HID/keyboard output) must
// go through these so they agree on the same bounds — a value that falls
// through both (a reserved 4-15 value, or a button index >=
// kBindButtonCount) is unbound, not a crash.
static const uint8_t kBindButtonCount = 32;  // matches the HID report's button mask
inline bool stickBindIsDirection(uint8_t b) { return b <= kBindRight; }
inline bool stickBindIsButton(uint8_t b) {
  return b >= kBindButton0 && b < kBindButton0 + kBindButtonCount;
}
inline uint8_t stickBindButton(uint8_t b) { return (uint8_t)(b - kBindButton0); }

enum StickShape : uint8_t { kShapeRect = 0, kShapeRound = 1 };

// Same-platform format: this struct is memcpy'd straight to/from the SD card
// (Task 9), not serialised field-by-field, so its layout IS the on-disk
// format. That is only safe because every device that reads or writes it —
// the ESP32-P4 and this host test build — is little-endian; no byte-swapping
// is done or needed, and that assumption is not re-checked anywhere.
//
// Field order is deliberate: `color` (2-byte) sits before the run of 1-byte
// fields so nothing needs inter-field padding, and `reserved` soaks up what
// would otherwise be a compiler-inserted pad byte at the end (which
// aggregate initialisation does not zero) — a stray byte there would make
// two saves of the identical layout differ byte-for-byte, breaking any
// future CRC, dedupe, or "did the editor actually change anything?" memcmp,
// and it means uninitialised stack contents get written to storage.
struct StickKey {
  char label[8];
  int16_t x, y, w, h;
  uint16_t color;
  uint8_t shape;
  uint8_t bind;      // StickBind
  uint8_t key;       // keycode used in keyboard output mode
  uint8_t reserved;  // must stay 0; free byte for a future per-key flag
};
static_assert(sizeof(StickKey) == 22,
              "StickKey is memcpy'd to SD; bump kProfileFormatVersion if this changes");

struct StickProfile {
  char name[16];
  StickKey keys[STICK_LAYOUT_MAX_KEYS];
  uint8_t keyCount;
  uint8_t socdPolicy;
};

// Resolved input state for one poll.
struct StickState {
  uint32_t buttons;   // bit N = gamepad button N held
  uint32_t keysHeld;  // bit N = key index N has a live contact, set for
                       // EVERY hit regardless of bind (including kBindNone).
                       // This is the only signal that survives SOCD
                       // collapsing two opposing directions to neutral while
                       // both keys are still physically held — up/down/left/
                       // right and buttons alone cannot be inverted back
                       // into "which keys are touched". STICK_LAYOUT_MAX_KEYS
                       // is 20, comfortably inside the 32 bits here.
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
