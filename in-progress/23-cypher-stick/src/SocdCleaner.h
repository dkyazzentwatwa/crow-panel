#ifndef CYPHER_STICK_SOCD_CLEANER_H
#define CYPHER_STICK_SOCD_CLEANER_H

#include <stdint.h>

// SOCD (Simultaneous Opposing Cardinal Directions) resolution.
//
// Deliberately free of Arduino.h so scripts/test-cypher-stick.sh compiles the
// SHIPPING source, not a copy of it.
//
// Values match Arduino-ESP32's HAT_* constants (USBHIDGamepad.h) exactly.
// TinyUSB itself spells the same values GAMEPAD_HAT_*/GAMEPAD_HAT_CENTERED;
// this project talks to the Arduino-ESP32 HID API, so HAT_* is the one that
// matters here. CrowGamepadTransport (Task 5) will static_assert that they
// still agree — nothing enforces that today.
enum StickHat : uint8_t {
  kHatCenter = 0,
  kHatUp = 1,
  kHatUpRight = 2,
  kHatRight = 3,
  kHatDownRight = 4,
  kHatDown = 5,
  kHatDownLeft = 6,
  kHatLeft = 7,
  kHatUpLeft = 8,
};

enum SocdPolicy : uint8_t {
  kSocdNeutral = 0,    // both held -> neutral on that axis
  kSocdLastInput = 1,  // most recently pressed wins
  kSocdFirstInput = 2, // the one held first wins
  kSocdUpPriority = 3, // up beats down; horizontal falls back to neutral
};

// Ordering state the Last/First policies need. Caller owns one per stick.
struct SocdMemory {
  bool prevUp = false, prevDown = false, prevLeft = false, prevRight = false;
  uint8_t hWinner = 0;  // 0 none, 1 left, 2 right
  uint8_t vWinner = 0;  // 0 none, 1 up, 2 down
};

// Resolve the four held directions to one hat value, updating `mem`.
// Call exactly once per poll: the Last/First policies infer press order from
// the transition between calls.
uint8_t socdResolve(bool up, bool down, bool left, bool right,
                    uint8_t policy, SocdMemory &mem);

#endif
