# Cypher Stick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `in-progress/23-cypher-stick`, a playable leverless-style touch fightstick that presents to a PC or Nintendo Switch as a real USB gamepad, with an on-panel drag-and-drop layout editor and per-game profiles.

**Architecture:** Touch contacts are hit-tested against a profile of button rects to produce a held-button bitmask plus four direction booleans. The directions pass through a pure SOCD cleaner that yields a single 0–8 hat value, and the complete state (hat + 32-bit button mask) goes to `USBHIDGamepad::send()` as **one atomic USB report**, emitted only when it differs from the last. The touch→HID path is pinned to core 1 at high priority; all rendering, SD, audio, and serial work stays on core 0, because the DSI panel is single-framebuffer and one redraw costs tens of milliseconds.

**Tech Stack:** Arduino-CLI + `esp32:esp32@3.3.8`, `USBHIDGamepad` from the core's USB library (no custom HID descriptor needed), Arduino_GFX for rendering (no LVGL), GT911 multi-touch via `CrowDisplay::touchPoints()`, g++ for host-side tests.

**Spec:** [`docs/superpowers/specs/2026-08-01-touch-fightstick-design.md`](../specs/2026-08-01-touch-fightstick-design.md)

---

## Before you start

**Read these first:**
- `CLAUDE.md` — especially the three-layer feature-flag rule and the ctags workaround
- The spec linked above

**Three rules that will silently break your build if ignored:**

1. **`CTAGS_WORKAROUND=1` is required on this machine.** It skips Arduino's prototype generation, so **every function in a `.ino` must be defined before it is used**. Preserve that ordering.
2. **Flags gating shared-library code must be passed as `-D`.** `shared/CrowPanelShared/*.cpp` never sees a project's `ProjectConfig.h`. A flag set only in `ProjectConfig.h` will appear to work in the sketch and be silently off inside the shared library.
3. **Never wrap a feature-flagged library include in `__has_include`.** It disables the feature and still builds green. Verify linkage by checking `<build-path>/libraries/`, not by a green compile.

**The testing standard for every host-tested task in this plan.** Task 2's
review established it the hard way: its first version passed 96 checks while
three real code mutations survived untouched, because two-thirds of those checks
asserted a property the type system already guaranteed.

Before calling any test task done, mutate the implementation and confirm a test
fails. Specifically:

- **Assert behaviour, not invariants that cannot be violated.** "The result is
  in range 0..8" is worthless if the function structurally cannot return
  anything else.
- **Prefer properties over point cases.** "The hat never contains a direction
  that is not held" caught two wrong-diagonal mutations that four hand-written
  point assertions missed.
- **Test both sides of anything symmetric.** Task 2 tested the horizontal axis
  thoroughly and the vertical axis not at all, so deleting `mem.prevUp = up;`
  passed every check while deleting `mem.prevLeft` failed nine.
- **Carry state across calls when the function is stateful.** A fresh state
  object per loop iteration only ever explores first-call behaviour.

**Shared-API names that are easy to guess wrong** (each of these was verified
against the header while writing this plan):

| Correct | Not |
|---|---|
| `CrowDisplay::canvas()` | `CrowDisplay::gfx()` |
| `CrowDisplay::begin(activeHardwareProfile(), "Title", manualFlush)` | `begin()` with no profile |
| `gRouter.begin(Serial, "App Name")` — `Stream&` plus a name | `begin(&Serial)` |
| `gRouter.printHelp()` — no argument, and `begin()` registers `help` for you | `printHelp(Serial)` |
| `gRouter.poll()` in `loop()` | `gRouter.tick()` — no such method |
| `gEvents.add("msg")` — `EventLog` has no `begin()` | `gEvents.begin()` |
| `gUiTouch.tick()` — `CrowTouch` *does* use `tick()` | (only the router differs) |
| `Widgets::kAccent` etc., RGB565 `uint16_t` | `UiTheme::accent()` — that struct is `uint32_t` and unrelated |

**The two FQBNs used in this plan:**

```bash
# Default (mock HID) — used by compile-all and most flag-matrix rows
esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

```bash
# USB-OTG — REQUIRED for a live gamepad. Without it USBHIDGamepad falls back to MOCK.
esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

**Scope note:** Palm rejection is limited to ignoring contacts that land outside every key rect. Plumbing the GT911's per-point size bytes through `DisplayBringup` is explicitly **out of scope** for this plan.

---

## File Structure

**Shared library (affects projects 05, 18, 21, 22 — keep changes additive):**

| File | Responsibility |
|---|---|
| `shared/CrowPanelShared/AppConfig.h` | add `USE_USB_GAMEPAD` and `CROW_TOUCH_SAMPLE_MS` (default `8`, unchanged behaviour) |
| `shared/CrowPanelShared/CrowGamepadTransport.{h,cpp}` | **new.** Wraps `USBHIDGamepad`. Standalone — deliberately *not* a `HidTransport` subclass, because that interface is keyboard/consumer/mouse-shaped and a gamepad fits none of it |
| `shared/CrowPanelShared/CrowHidBackend.{h,cpp}` | add `gamepadState(hat, buttons)` and `keyboardHeldState(keys, count)` — both change-detected, both bypassing `tapKey()`/`kHoldMs` |
| `shared/CrowPanelShared/CrowHidTransport.h` | add `supportsHeldKeys()`/`keyPressHeld()`/`keyReleaseHeld()` as **virtual with default no-op bodies**, so BLE and projects 05/21 need no change |
| `shared/CrowPanelShared/CrowUsbTransport.{h,cpp}` | override the held-key methods with `gKeyboard.press()`/`release()` |
| `shared/CrowPanelShared/DisplayBringup.cpp` | replace the hardcoded 8 ms GT911 sample throttle at line 97 with `CROW_TOUCH_SAMPLE_MS` |
| `shared/CrowPanelShared/CrowHid.h` | include the new transport header |

**Project `in-progress/23-cypher-stick/`:**

| File | Responsibility |
|---|---|
| `23-cypher-stick.ino` | globals, serial commands, `setup()`/`loop()` |
| `config/ProjectConfig.h` | flag + tuning overrides |
| `src/SocdCleaner.{h,cpp}` | pure direction→hat resolution. **No `Arduino.h`** so it host-tests |
| `src/StickLayout.{h,cpp}` | `StickKey`/`StickProfile` models + hit-testing. **No `Arduino.h`** |
| `src/StickProfiles.{h,cpp}` | versioned binary serialisation. **No `Arduino.h`** |
| `src/StickStorage.{h,cpp}` | SD load/save/list wrapping the serialiser |
| `src/StickEngine.{h,cpp}` | the core-1 hot loop |
| `src/StickRender.{h,cpp}` | PLAY view, delta-drawn with per-key region flush |
| `src/StickEditor.{h,cpp}` | drag / resize / rebind UI |
| `src/StickTouch.{h,cpp}` | `KeysTouch` fork retuned for latency |
| `src/StickAudio.{h,cpp}` | per-press click on core 0 |
| `test/host_main.cpp` | g++ test harness |

Colours come from the RGB565 `Widgets::` palette in `DashboardWidgets.h`
(`kBg`, `kSurface`, `kAccent`, `kLine`, `kTextHi`, `kTextMut`). The `UiTheme`
struct holds `uint32_t` values for a different purpose and is **not** what
Arduino_GFX takes.

**Scripts/docs:**
- `scripts/project-registry.sh` — add to `crowpanel_inprogress_projects`
- `scripts/test-cypher-stick.sh` — **new**
- `scripts/check-flag-matrix.sh` — new rows
- `README.md`, `docs/full-port-proof-matrix.md`, project `README.md` + `TECHNICAL.md`

---

## Task 1: Project scaffold that compiles

**Files:**
- Create: `in-progress/23-cypher-stick/23-cypher-stick.ino`
- Create: `in-progress/23-cypher-stick/config/ProjectConfig.h`
- Modify: `scripts/project-registry.sh`

- [ ] **Step 1: Create `config/ProjectConfig.h`**

```cpp
#ifndef CYPHER_STICK_PROJECT_CONFIG_H
#define CYPHER_STICK_PROJECT_CONFIG_H

// Cypher Stick — touch fightstick. Flags default off (mock-first); real builds
// pass -D flags so the shared library sees them too (see CLAUDE.md).

// Max keys in one profile. Sizes fixed arrays — do not raise casually.
#ifndef STICK_MAX_KEYS
#define STICK_MAX_KEYS 20
#endif

// Profiles held in RAM / stored on SD.
#ifndef STICK_MAX_PROFILES
#define STICK_MAX_PROFILES 8
#endif

// A lift is committed after this many consecutive empty polls. 1 keeps latency
// at one poll; raise ONLY if hardware shows the GT911 dropping frames mid-hold.
// Project 21 uses a 30 ms wall-clock timer here; that is ~2 frames and is
// exactly what this project exists to avoid.
#ifndef STICK_LIFT_CONFIRM_POLLS
#define STICK_LIFT_CONFIRM_POLLS 1
#endif

// Stick task cadence on core 1. The GT911 reports at 100 Hz, so polling much
// faster than this only burns I2C bandwidth.
#ifndef STICK_POLL_MS
#define STICK_POLL_MS 2
#endif

#include <AppConfig.h>

#endif
```

- [ ] **Step 2: Create the sketch**

Note the function-before-use ordering — required by the ctags workaround.

```cpp
#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

static EventLog gEvents;
static SerialCommandRouter gRouter;

static void cmdStatus(const String &args) {
  (void)args;
  Serial.println("Cypher Stick — scaffold");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  // NOTE: begin() takes a Stream REFERENCE plus an app name, and registers its
  // own `help`. EventLog has no begin() — it is add()-only.
  gRouter.begin(Serial, "Cypher Stick");
  gRouter.on("status", "show stick status", cmdStatus);
  gEvents.add("boot");
  Serial.println("Cypher Stick ready");
}

void loop() {
  gRouter.poll();
}
```

- [ ] **Step 3: Register the project**

In `scripts/project-registry.sh`, add to `crowpanel_inprogress_projects()`, keeping numeric order (after `in-progress/19-starbeam-console`):

```
in-progress/23-cypher-stick
```

- [ ] **Step 4: Compile**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick --build-property tools.ctags.cmd.path=/usr/bin/true in-progress/23-cypher-stick
```

Expected: compiles green.

If you see `expected constructor, destructor, or type conversion` or `'cmdStatus' was not declared in this scope`, a function is being used before it is defined — reorder.

- [ ] **Step 5: Commit**

```bash
git add in-progress/23-cypher-stick scripts/project-registry.sh
git commit -m "feat(23): scaffold Cypher Stick project"
```

---

## Task 2: SOCD cleaner (pure, host-tested)

The hat is a single 0–8 enum, so left+right is unrepresentable on the wire. This code only decides *which* legal value results.

> **Revised after code review (commit `2369366`). The shipped source is the
> source of truth, not the listings below.** Two changes worth knowing if you
> read this task for reference:
>
> 1. **`resolveAxis()` no longer takes an `upPriorityWinsA` flag.** The
>    asymmetry is lifted to the call site — `hPolicy = (policy ==
>    kSocdUpPriority) ? kSocdNeutral : policy` for the horizontal axis — keeping
>    all policy logic in one switch and scaling to a future policy without
>    another bool.
> 2. **The exhaustive test was rebuilt.** The original swept 16 combos × 4
>    policies with a *fresh* `SocdMemory` each iteration, so it only explored
>    first-poll state and asserted a property (`hat <= 8`) that is structurally
>    impossible to violate. Mutation testing found three real code mutations
>    surviving all 96 checks. It now sweeps 1024 *transitions* with state carried
>    across each poll pair, plus three named regression tests for the tie-breaks.
>    1068 checks.
>
> The lesson generalises to every later task: a test that passes against both
> the correct and the mutated implementation is not a test. Assert behaviour,
> not invariants the types already guarantee.

**Files:**
- Create: `in-progress/23-cypher-stick/src/SocdCleaner.h`
- Create: `in-progress/23-cypher-stick/src/SocdCleaner.cpp`
- Create: `in-progress/23-cypher-stick/test/host_main.cpp`
- Create: `scripts/test-cypher-stick.sh`

- [ ] **Step 1: Write the header**

`SocdCleaner.h` must not include `Arduino.h` — that is what makes it host-testable.

```cpp
#ifndef CYPHER_STICK_SOCD_CLEANER_H
#define CYPHER_STICK_SOCD_CLEANER_H

#include <stdint.h>

// SOCD (Simultaneous Opposing Cardinal Directions) resolution.
//
// Deliberately free of Arduino.h so scripts/test-cypher-stick.sh compiles the
// SHIPPING source, not a copy of it.
//
// Values match TinyUSB's HAT_* constants exactly; CrowGamepadTransport
// static_asserts that they still agree.
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
```

- [ ] **Step 2: Write the failing test**

Create `test/host_main.cpp`:

```cpp
// Host-side tests for project 23. Compiles the SHIPPING sources, not copies —
// SocdCleaner.h and StickLayout.h are deliberately free of Arduino.h so this
// is possible. Run via scripts/test-cypher-stick.sh; no board required.

#include "../src/SocdCleaner.h"

#include <cstdio>

static int gFail = 0;
static int gRun = 0;

static void check(bool ok, const char *what, const char *detail = "") {
  gRun++;
  if (!ok) {
    gFail++;
    std::printf("  FAIL %s %s\n", what, detail);
  }
}

static void testSocdSingleDirections() {
  std::printf("socd: single directions pass through on every policy\n");
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    SocdMemory m;
    check(socdResolve(true, false, false, false, p, m) == kHatUp, "up");
    SocdMemory m2;
    check(socdResolve(false, true, false, false, p, m2) == kHatDown, "down");
    SocdMemory m3;
    check(socdResolve(false, false, true, false, p, m3) == kHatLeft, "left");
    SocdMemory m4;
    check(socdResolve(false, false, false, true, p, m4) == kHatRight, "right");
    SocdMemory m5;
    check(socdResolve(false, false, false, false, p, m5) == kHatCenter, "none");
  }
}

static void testSocdDiagonals() {
  std::printf("socd: legal diagonals\n");
  SocdMemory m;
  check(socdResolve(true, false, false, true, kSocdNeutral, m) == kHatUpRight, "up+right");
  SocdMemory m2;
  check(socdResolve(false, true, true, false, kSocdNeutral, m2) == kHatDownLeft, "down+left");
}

static void testSocdNeutral() {
  std::printf("socd: neutral policy\n");
  SocdMemory m;
  check(socdResolve(false, false, true, true, kSocdNeutral, m) == kHatCenter, "L+R -> center");
  SocdMemory m2;
  check(socdResolve(true, true, false, false, kSocdNeutral, m2) == kHatCenter, "U+D -> center");
}

static void testSocdUpPriority() {
  std::printf("socd: up priority\n");
  SocdMemory m;
  check(socdResolve(true, true, false, false, kSocdUpPriority, m) == kHatUp, "U+D -> up");
  SocdMemory m2;
  check(socdResolve(false, false, true, true, kSocdUpPriority, m2) == kHatCenter,
        "L+R falls back to neutral");
}

static void testSocdLastInput() {
  std::printf("socd: last input priority\n");
  SocdMemory m;
  // Hold left, then add right: right is newest and wins.
  check(socdResolve(false, false, true, false, kSocdLastInput, m) == kHatLeft, "left first");
  check(socdResolve(false, false, true, true, kSocdLastInput, m) == kHatRight, "right added wins");
  // Release right; left alone resumes.
  check(socdResolve(false, false, true, false, kSocdLastInput, m) == kHatLeft, "left resumes");
}

static void testSocdFirstInput() {
  std::printf("socd: first input priority\n");
  SocdMemory m;
  check(socdResolve(false, false, true, false, kSocdFirstInput, m) == kHatLeft, "left first");
  check(socdResolve(false, false, true, true, kSocdFirstInput, m) == kHatLeft, "left keeps it");
  check(socdResolve(false, false, false, true, kSocdFirstInput, m) == kHatRight, "left released");
}

static void testSocdAllCombosLegal() {
  std::printf("socd: all 16 combos x 4 policies produce a legal hat\n");
  for (uint8_t p = kSocdNeutral; p <= kSocdUpPriority; p++) {
    for (int bits = 0; bits < 16; bits++) {
      SocdMemory m;
      uint8_t hat = socdResolve(bits & 1, bits & 2, bits & 4, bits & 8, p, m);
      char d[64];
      std::snprintf(d, sizeof d, "policy=%u bits=%d hat=%u", p, bits, hat);
      check(hat <= kHatUpLeft, "hat within 0..8", d);
    }
  }
}

int main() {
  testSocdSingleDirections();
  testSocdDiagonals();
  testSocdNeutral();
  testSocdUpPriority();
  testSocdLastInput();
  testSocdFirstInput();
  testSocdAllCombosLegal();
  std::printf("\n%d checks, %d failures\n", gRun, gFail);
  return gFail ? 1 : 0;
}
```

- [ ] **Step 3: Create the test script**

`scripts/test-cypher-stick.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail

# Host-side tests for project 23 (Cypher Stick). No board required.
#
# Covers the logic that is silent when it goes wrong: SOCD resolution (a wrong
# hat value is a wrong input, not a crash), key hit-testing, and profile
# serialisation. These compile the shipping src/ headers, which are deliberately
# free of Arduino.h so this is possible.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/in-progress/23-cypher-stick"
OUT="${BUILD_ROOT:-$ROOT/_arduino-build}/cypher-stick-host"
CXX="${CXX:-g++}"

mkdir -p "$OUT"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$PROJECT/test/host_main.cpp" \
  "$PROJECT/src/SocdCleaner.cpp" \
  -o "$OUT/cypher-stick-tests"

"$OUT/cypher-stick-tests" "$@"
```

```bash
chmod +x scripts/test-cypher-stick.sh
```

- [ ] **Step 4: Run to verify it fails**

```bash
./scripts/test-cypher-stick.sh
```

Expected: FAIL — `SocdCleaner.cpp` does not exist, so the link fails.

- [ ] **Step 5: Implement**

`src/SocdCleaner.cpp`:

```cpp
#include "SocdCleaner.h"

namespace {

// Resolve one opposing pair. `a` is the negative direction (left/up),
// `b` the positive (right/down). Returns 0 none, 1 a, 2 b.
uint8_t resolveAxis(bool a, bool b, bool prevA, bool prevB, uint8_t policy,
                    uint8_t &winner, bool upPriorityWinsA) {
  if (!a && !b) {
    winner = 0;
    return 0;
  }
  if (a && !b) {
    winner = 0;
    return 1;
  }
  if (!a && b) {
    winner = 0;
    return 2;
  }

  // Both held.
  switch (policy) {
    case kSocdLastInput: {
      bool aIsNew = a && !prevA;
      bool bIsNew = b && !prevB;
      if (aIsNew && !bIsNew) winner = 1;
      else if (bIsNew && !aIsNew) winner = 2;
      // Both new on the same poll, or neither new: keep the standing winner.
      return winner;
    }
    case kSocdFirstInput: {
      if (winner == 0) {
        // Whichever was already held before this poll got there first.
        if (prevA && !prevB) winner = 1;
        else if (prevB && !prevA) winner = 2;
        // Both arrived together: no first, stay neutral.
      }
      return winner;
    }
    case kSocdUpPriority:
      winner = 0;
      return upPriorityWinsA ? 1 : 0;  // vertical: up wins. horizontal: neutral.
    case kSocdNeutral:
    default:
      winner = 0;
      return 0;
  }
}

}  // namespace

uint8_t socdResolve(bool up, bool down, bool left, bool right,
                    uint8_t policy, SocdMemory &mem) {
  uint8_t v = resolveAxis(up, down, mem.prevUp, mem.prevDown, policy,
                          mem.vWinner, /*upPriorityWinsA=*/true);
  uint8_t h = resolveAxis(left, right, mem.prevLeft, mem.prevRight, policy,
                          mem.hWinner, /*upPriorityWinsA=*/false);

  mem.prevUp = up;
  mem.prevDown = down;
  mem.prevLeft = left;
  mem.prevRight = right;

  // Combine into the hat enum.
  const bool u = (v == 1), d = (v == 2), l = (h == 1), r = (h == 2);
  if (u && r) return kHatUpRight;
  if (d && r) return kHatDownRight;
  if (d && l) return kHatDownLeft;
  if (u && l) return kHatUpLeft;
  if (u) return kHatUp;
  if (d) return kHatDown;
  if (l) return kHatLeft;
  if (r) return kHatRight;
  return kHatCenter;
}
```

- [ ] **Step 6: Run to verify it passes**

```bash
./scripts/test-cypher-stick.sh
```

Expected: `N checks, 0 failures`, exit 0.

- [ ] **Step 7: Commit**

```bash
git add in-progress/23-cypher-stick/src/SocdCleaner.h in-progress/23-cypher-stick/src/SocdCleaner.cpp in-progress/23-cypher-stick/test/host_main.cpp scripts/test-cypher-stick.sh
git commit -m "feat(23): add host-tested SOCD cleaner"
```

---

## Task 3: Layout model and hit-testing

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickLayout.h`
- Create: `in-progress/23-cypher-stick/src/StickLayout.cpp`
- Modify: `in-progress/23-cypher-stick/test/host_main.cpp`
- Modify: `scripts/test-cypher-stick.sh`

- [ ] **Step 1: Write the header**

Also free of `Arduino.h`.

```cpp
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
```

- [ ] **Step 2: Write the failing tests**

Add to `test/host_main.cpp` — insert the include at the top next to the existing one:

```cpp
#include "../src/StickLayout.h"
```

Add these functions before `main()`:

```cpp
static void testHitTest() {
  std::printf("layout: hit testing\n");
  StickProfile p;
  stickDefaultProfile(p);
  check(p.keyCount == 10, "default profile has 4 directions + 6 buttons");

  // A point in the middle of key 0 hits key 0.
  const StickKey &k = p.keys[0];
  check(stickHitTest(p, k.x + k.w / 2, k.y + k.h / 2) == 0, "centre of key 0");
  // Far outside everything.
  check(stickHitTest(p, 5, 5) == -1, "top-left corner hits nothing");
  // Exact edges are inside; one past is not.
  check(stickHitTest(p, k.x, k.y) == 0, "top-left edge inclusive");
  check(stickHitTest(p, k.x + k.w, k.y) == -1, "right edge exclusive");
}

static void testHitTestOverlapPrefersLater() {
  std::printf("layout: overlapping keys — later wins\n");
  StickProfile p;
  p.keyCount = 2;
  p.socdPolicy = kSocdNeutral;
  p.keys[0] = {"A", 100, 100, 100, 100, kShapeRect, 0, kBindButton0, 'a'};
  p.keys[1] = {"B", 150, 150, 100, 100, kShapeRect, 0, kBindButton0 + 1, 'b'};
  check(stickHitTest(p, 175, 175) == 1, "overlap region resolves to key 1");
  check(stickHitTest(p, 110, 110) == 0, "key 0 only region");
}

static void testResolveIgnoresMisses() {
  std::printf("layout: contacts outside every key are dropped\n");
  StickProfile p;
  p.keyCount = 2;
  p.socdPolicy = kSocdNeutral;
  p.keys[0] = {"L", 100, 100, 80, 80, kShapeRect, 0, kBindLeft, 'a'};
  p.keys[1] = {"P", 400, 100, 80, 80, kShapeRect, 0, kBindButton0, 'b'};

  const int hits[3] = {0, -1, 1};  // middle contact is a resting palm
  StickState s = stickResolve(p, hits, 3);
  check(s.left, "left direction held");
  check(s.buttons == 1u, "button 0 held, nothing else");
  check(!s.up && !s.down && !s.right, "no other directions");
}

static void testResolveEmpty() {
  std::printf("layout: no contacts -> nothing held\n");
  StickProfile p;
  stickDefaultProfile(p);
  StickState s = stickResolve(p, nullptr, 0);
  check(s.buttons == 0u, "no buttons");
  check(!s.up && !s.down && !s.left && !s.right, "no directions");
}

// The three below exist because Task 2's review found that tests which only
// assert the cases you thought of miss whole branches. These pin branches the
// obvious tests do not reach.

static void testRoundShapeHitTest() {
  std::printf("layout: round keys reject the bounding-box corners\n");
  StickProfile p;
  p.keyCount = 1;
  p.socdPolicy = kSocdNeutral;
  p.keys[0] = {"O", 100, 100, 100, 100, kShapeRound, 0, kBindButton0, 'a'};
  check(stickHitTest(p, 150, 150) == 0, "dead centre hits");
  check(stickHitTest(p, 150, 105) == 0, "top mid-edge hits");
  check(stickHitTest(p, 105, 105) == -1, "top-left corner is inside the box but outside the circle");
  check(stickHitTest(p, 195, 195) == -1, "bottom-right corner likewise");
  // Same rect as a square key must accept the corner — proves the shape branch
  // is what rejected it, not the bounds check.
  p.keys[0].shape = kShapeRect;
  check(stickHitTest(p, 105, 105) == 0, "as a rect, the corner hits");
}

static void testResolveRejectsOutOfRangeIndex() {
  std::printf("layout: hit indices past keyCount are ignored, not read\n");
  StickProfile p;
  p.keyCount = 1;
  p.socdPolicy = kSocdNeutral;
  p.keys[0] = {"P", 0, 0, 50, 50, kShapeRect, 0, kBindButton0, 'a'};
  const int hits[2] = {0, 7};  // 7 is past keyCount — must not be dereferenced
  StickState s = stickResolve(p, hits, 2);
  check(s.buttons == 1u, "only the valid key registered");
}

static void testResolveIgnoresButtonIndexAbove31() {
  std::printf("layout: a bind past button 31 cannot shift out of the mask\n");
  StickProfile p;
  p.keyCount = 1;
  p.socdPolicy = kSocdNeutral;
  // kBindButton0 + 32 would be a 1u << 32 shift: undefined behaviour.
  p.keys[0] = {"X", 0, 0, 50, 50, kShapeRect, 0, (uint8_t)(kBindButton0 + 32), 'a'};
  const int hits[1] = {0};
  StickState s = stickResolve(p, hits, 1);
  check(s.buttons == 0u, "out-of-range button contributes nothing");
}
```

Call them in `main()` before the summary printf:

```cpp
  testHitTest();
  testHitTestOverlapPrefersLater();
  testResolveIgnoresMisses();
  testResolveEmpty();
  testRoundShapeHitTest();
  testResolveRejectsOutOfRangeIndex();
  testResolveIgnoresButtonIndexAbove31();
```

- [ ] **Step 3: Add the new source to the test script**

In `scripts/test-cypher-stick.sh`, add the layout source to the compile line so it reads:

```bash
  "$PROJECT/test/host_main.cpp" \
  "$PROJECT/src/SocdCleaner.cpp" \
  "$PROJECT/src/StickLayout.cpp" \
```

- [ ] **Step 4: Run to verify it fails**

```bash
./scripts/test-cypher-stick.sh
```

Expected: FAIL — `StickLayout.cpp` does not exist.

- [ ] **Step 5: Implement**

`src/StickLayout.cpp`:

```cpp
#include "StickLayout.h"

#include <string.h>

int stickHitTest(const StickProfile &p, int16_t x, int16_t y) {
  // Iterate backwards so the last (topmost) key wins an overlap.
  for (int i = (int)p.keyCount - 1; i >= 0; i--) {
    const StickKey &k = p.keys[i];
    if (x < k.x || x >= k.x + k.w) continue;
    if (y < k.y || y >= k.y + k.h) continue;
    if (k.shape == kShapeRound) {
      const int32_t cx = k.x + k.w / 2;
      const int32_t cy = k.y + k.h / 2;
      const int32_t rx = k.w / 2;
      const int32_t ry = k.h / 2;
      if (rx <= 0 || ry <= 0) continue;
      const int32_t dx = x - cx;
      const int32_t dy = y - cy;
      // Normalised ellipse test in fixed point, no floating point on the
      // stick task.
      if ((dx * dx * 10000) / (rx * rx) + (dy * dy * 10000) / (ry * ry) > 10000) {
        continue;
      }
    }
    return i;
  }
  return -1;
}

StickState stickResolve(const StickProfile &p, const int *hits, int hitCount) {
  StickState s = {0, false, false, false, false};
  for (int i = 0; i < hitCount; i++) {
    const int idx = hits[i];
    if (idx < 0 || idx >= (int)p.keyCount) continue;  // palm / stray contact
    const uint8_t bind = p.keys[idx].bind;
    switch (bind) {
      case kBindUp: s.up = true; break;
      case kBindDown: s.down = true; break;
      case kBindLeft: s.left = true; break;
      case kBindRight: s.right = true; break;
      case kBindNone: break;
      default:
        if (bind >= kBindButton0) {
          const uint8_t b = bind - kBindButton0;
          if (b < 32) s.buttons |= (1u << b);
        }
        break;
    }
  }
  return s;
}

void stickDefaultProfile(StickProfile &p) {
  memset(&p, 0, sizeof p);
  strncpy(p.name, "Default", sizeof p.name - 1);
  p.socdPolicy = kSocdUpPriority;

  // Left hand: four directions, leverless arrangement. Panel is 1024x600.
  const int16_t s = 96;   // key size
  const int16_t g = 12;   // gap
  const int16_t lx = 60;  // left cluster origin
  const int16_t ly = 300;

  struct Seed { const char *label; int16_t x, y; uint8_t bind; uint8_t key; };
  const Seed seeds[] = {
    {"<",  lx,                 ly,             kBindLeft,  'a'},
    {"v",  lx + s + g,         ly,             kBindDown,  's'},
    {">",  lx + 2 * (s + g),   ly,             kBindRight, 'd'},
    {"^",  lx + s + g,         ly + s + g,     kBindUp,    ' '},
    // Right hand: six attack buttons, two rows of three.
    {"LP", 540,                ly - s - g,     kBindButton0 + 0, 'u'},
    {"MP", 540 + s + g,        ly - s - g,     kBindButton0 + 1, 'i'},
    {"HP", 540 + 2 * (s + g),  ly - s - g,     kBindButton0 + 2, 'o'},
    {"LK", 540,                ly,             kBindButton0 + 3, 'j'},
    {"MK", 540 + s + g,        ly,             kBindButton0 + 4, 'k'},
    {"HK", 540 + 2 * (s + g),  ly,             kBindButton0 + 5, 'l'},
  };

  p.keyCount = (uint8_t)(sizeof seeds / sizeof seeds[0]);
  for (uint8_t i = 0; i < p.keyCount; i++) {
    StickKey &k = p.keys[i];
    strncpy(k.label, seeds[i].label, sizeof k.label - 1);
    k.label[sizeof k.label - 1] = '\0';
    k.x = seeds[i].x;
    k.y = seeds[i].y;
    k.w = s;
    k.h = s;
    k.shape = kShapeRound;
    k.color = 0;
    k.bind = seeds[i].bind;
    k.key = seeds[i].key;
  }
}
```

Note: `testHitTest` asserts a rect-style edge check on key 0, but the default profile uses `kShapeRound`. Change key 0's centre assertions only — the edge assertions must use a rect key. Replace the two edge checks in `testHitTest` with:

```cpp
  StickProfile r;
  r.keyCount = 1;
  r.socdPolicy = kSocdNeutral;
  r.keys[0] = {"R", 200, 200, 100, 100, kShapeRect, 0, kBindButton0, 'a'};
  check(stickHitTest(r, 200, 200) == 0, "top-left edge inclusive");
  check(stickHitTest(r, 300, 200) == -1, "right edge exclusive");
```

- [ ] **Step 6: Run to verify it passes**

```bash
./scripts/test-cypher-stick.sh
```

Expected: `N checks, 0 failures`.

- [ ] **Step 7: Commit**

```bash
git add in-progress/23-cypher-stick/src/StickLayout.h in-progress/23-cypher-stick/src/StickLayout.cpp in-progress/23-cypher-stick/test/host_main.cpp scripts/test-cypher-stick.sh
git commit -m "feat(23): add layout model and hit-testing"
```

---

## Task 4: Gamepad transport in the shared library

**Files:**
- Modify: `shared/CrowPanelShared/AppConfig.h`
- Create: `shared/CrowPanelShared/CrowGamepadTransport.h`
- Create: `shared/CrowPanelShared/CrowGamepadTransport.cpp`
- Modify: `shared/CrowPanelShared/CrowHid.h`

- [ ] **Step 1: Add the flag to `AppConfig.h`**

Immediately after the existing `USE_BLE_HID` block (around line 79):

```cpp
// USE_USB_GAMEPAD=1 exposes a native USB gamepad (TinyUSB HID, 32 buttons +
// 8-way hat) for project 23. Like USE_USB_HID it needs an USBMode=default
// (USB-OTG) FQBN; under the suite default USBMode=hwcdc it falls back to MOCK.
// It gates shared-library code, so real builds MUST pass -DUSE_USB_GAMEPAD=1
// (see the three-layer flag rule in CLAUDE.md).
#ifndef USE_USB_GAMEPAD
#define USE_USB_GAMEPAD 0
#endif
```

- [ ] **Step 2: Write the transport header**

Deliberately **not** a `HidTransport` subclass — that interface is
`keyDown`/`mouseMove`-shaped and a gamepad implements none of it.

```cpp
#ifndef CROW_HID_GAMEPAD_TRANSPORT_H
#define CROW_HID_GAMEPAD_TRANSPORT_H

#include "AppConfig.h"

#include <Arduino.h>

// Native USB gamepad output (project 23, Cypher Stick).
//
// This is NOT a HidTransport: that interface is keyboard/consumer/mouse-shaped
// and a gamepad implements none of it. HidBackend routes to this class
// directly via gamepadState().
//
// The one rule that matters here: emit whole state with a single send(). The
// core's USBHIDGamepad::pressButton()/releaseButton()/hat() each write their
// own USB report, so pressing three buttons would cost three reports and three
// frames of skew. sendState() writes exactly one report for the whole stick.
#if USE_USB_GAMEPAD && defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#define CROW_GAMEPAD_USB_LIVE 1
#elif USE_USB_GAMEPAD
#define CROW_GAMEPAD_USB_LIVE 0
#warning "USE_USB_GAMEPAD=1 but this is not an USBMode=default build (ARDUINO_USB_MODE!=0): native USB gamepad falls back to MOCK. Build with an USBMode=default FQBN for a live gamepad."
#else
#define CROW_GAMEPAD_USB_LIVE 0
#endif

class GamepadTransport {
 public:
  void begin();
  bool ready() const { return CROW_GAMEPAD_USB_LIVE; }
  const char *name() const { return CROW_GAMEPAD_USB_LIVE ? "PAD" : "PAD-MOCK"; }

  // One atomic HID report: hat (0-8) plus a 32-bit button mask.
  void sendState(uint8_t hat, uint32_t buttons);

  uint32_t reports() const { return reports_; }

 private:
  uint32_t reports_ = 0;
  bool begun_ = false;
};

#endif
```

- [ ] **Step 3: Write the transport implementation**

```cpp
#include "CrowGamepadTransport.h"

#if CROW_GAMEPAD_USB_LIVE
#include <USB.h>
#include <USBHIDGamepad.h>

static USBHIDGamepad gGamepad;

// Our StickHat values are passed straight through as TinyUSB HAT_* values.
// If the core ever renumbers these, this is where it breaks loudly.
static_assert(HAT_CENTER == 0 && HAT_UP == 1 && HAT_UP_RIGHT == 2 && HAT_RIGHT == 3 &&
                  HAT_DOWN_RIGHT == 4 && HAT_DOWN == 5 && HAT_DOWN_LEFT == 6 &&
                  HAT_LEFT == 7 && HAT_UP_LEFT == 8,
              "TinyUSB HAT_* values no longer match StickHat");
#endif

void GamepadTransport::begin() {
  if (begun_) return;
  begun_ = true;
#if CROW_GAMEPAD_USB_LIVE
  gGamepad.begin();
  USB.begin();
#endif
}

void GamepadTransport::sendState(uint8_t hat, uint32_t buttons) {
  if (hat > 8) hat = 0;
  reports_++;
#if CROW_GAMEPAD_USB_LIVE
  // All six axes centred: this is a leverless, there are no analog sticks.
  gGamepad.send(0, 0, 0, 0, 0, 0, hat, buttons);
#else
  (void)hat;
  (void)buttons;
#endif
}
```

- [ ] **Step 4: Add to the umbrella header**

In `shared/CrowPanelShared/CrowHid.h`, add after the `CrowBleTransport.h` include:

```cpp
#include "CrowGamepadTransport.h"
```

- [ ] **Step 5: Verify the shared library still compiles for every existing consumer**

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Expected: all projects green. This change is additive, so projects 05/18/21/22 must be unaffected.

- [ ] **Step 6: Commit**

```bash
git add shared/CrowPanelShared/AppConfig.h shared/CrowPanelShared/CrowGamepadTransport.h shared/CrowPanelShared/CrowGamepadTransport.cpp shared/CrowPanelShared/CrowHid.h
git commit -m "feat(shared): add USB gamepad transport behind USE_USB_GAMEPAD"
```

---

## Task 5: `HidBackend::gamepadState()`

**Files:**
- Modify: `shared/CrowPanelShared/CrowHidBackend.h`
- Modify: `shared/CrowPanelShared/CrowHidBackend.cpp`

- [ ] **Step 1: Declare the API**

In `CrowHidBackend.h`, add to the public section after the mouse methods:

```cpp
  // Gamepad (project 23). Deliberately bypasses tapKey()/kHoldMs: a fightstick
  // is all holds, and the 24 ms auto-release that makes the macro deck work
  // would make holding back to block impossible. Change-detected — a repeated
  // identical state costs nothing.
  void gamepadState(uint8_t hat, uint32_t buttons);
  bool gamepadLive() const;
  uint32_t gamepadReports() const;
```

Add to the private members:

```cpp
  GamepadTransport gamepad_;
  bool gamepadBegun_ = false;
  uint8_t lastHat_ = 0;
  uint32_t lastButtons_ = 0;
  bool gamepadStateValid_ = false;
```

Add the include near the top, next to the other transport includes:

```cpp
#include "CrowGamepadTransport.h"
```

- [ ] **Step 2: Implement**

In `CrowHidBackend.cpp`, add after `tapKey()`:

```cpp
void HidBackend::gamepadState(uint8_t hat, uint32_t buttons) {
  if (!gamepadBegun_) {
    gamepad_.begin();
    gamepadBegun_ = true;
  }
  // Change detection: the stick task calls this every poll, and an unchanged
  // state must not cost a USB report.
  if (gamepadStateValid_ && hat == lastHat_ && buttons == lastButtons_) return;
  lastHat_ = hat;
  lastButtons_ = buttons;
  gamepadStateValid_ = true;
  gamepad_.sendState(hat, buttons);
  reports_++;
}

bool HidBackend::gamepadLive() const { return gamepad_.ready(); }

uint32_t HidBackend::gamepadReports() const { return gamepad_.reports(); }
```

Note: no `record()` call. `record()` builds an Arduino `String`, and this runs on the stick task where heap allocation is forbidden.

- [ ] **Step 3: Compile everything**

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add shared/CrowPanelShared/CrowHidBackend.h shared/CrowPanelShared/CrowHidBackend.cpp
git commit -m "feat(shared): add HidBackend::gamepadState with change detection"
```

---

## Task 6: `StickTouch` — latency-retuned multi-contact tracking

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickTouch.h`
- Create: `in-progress/23-cypher-stick/src/StickTouch.cpp`

Fork `projects/21-cypher-keys-hid-deck/src/KeysTouch.{h,cpp}` — the repo already does this (P21's `KeysTouch` was forked from P09's `TouchTracker`).

- [ ] **Step 1: Copy the source**

```bash
cp projects/21-cypher-keys-hid-deck/src/KeysTouch.h in-progress/23-cypher-stick/src/StickTouch.h
cp projects/21-cypher-keys-hid-deck/src/KeysTouch.cpp in-progress/23-cypher-stick/src/StickTouch.cpp
```

- [ ] **Step 2: Rename the class and guards**

In both files, replace `KeysTouch` → `StickTouch`, `CYPHER_KEYS_KEYS_TOUCH_H` → `CYPHER_STICK_STICK_TOUCH_H`, and the include of `../config/ProjectConfig.h` stays as-is.

- [ ] **Step 3: Replace the header comment**

```cpp
// Multi-contact touch tracking for the fightstick, forked from project 21's
// KeysTouch and retuned for latency.
//
// The real change is the POLL RATE, not the debounce: every STICK_POLL_MS
// (2 ms) instead of 16 ms, so a press reaches the host ~14 ms sooner.
//
// The release debounce is KEPT, and keeping it is deliberate. Project 21 sized
// its 30 ms window to "bridge a few dropped 8 ms frames" of observed GT911
// flicker on this panel. Dropping that to one poll would convert every flicker
// into a spurious release — in a fighting game, a dropped block or a lost
// charge. A late release is survivable; a phantom one is not. STICK_LIFT_
// CONFIRM_MS is 24 ms, slightly tighter than P21 only because we sample ~4x
// more often.
//
// So: presses are fast, releases are safe, and the two are not symmetric.
//
// The GT911 tracks at most 5 contacts. That ceiling is hardware.
```

- [ ] **Step 4: Change the poll gate**

In `StickTouch.cpp`, find the poll-interval check using `CYPHER_KEYS_TOUCH_POLL_MS` and change it to `STICK_POLL_MS`.

- [ ] **Step 5: Retarget the release debounce (do NOT remove it)**

Keep the `Contact` struct's `releasePending` / `releasePendingSinceMs` fields
exactly as `KeysTouch` has them — the wall-clock mechanism is correct and
load-bearing. Only the constant changes, from
`CYPHER_KEYS_TOUCH_RELEASE_DEBOUNCE_MS` (30) to `STICK_LIFT_CONFIRM_MS` (24):

```cpp
    if (!c.seenThisPoll && c.active) {
      if (!c.releasePending) {
        c.releasePending = true;
        c.releasePendingSinceMs = now;
      } else if (now - c.releasePendingSinceMs >= STICK_LIFT_CONFIRM_MS) {
        c.active = false;
        c.releasedEdge = true;
        c.releasePending = false;
        c.owner = -1;
      }
    } else if (c.seenThisPoll) {
      c.releasePending = false;
    }
```

Verify that a PRESS still takes effect on the very first poll that sees the
contact — no debounce on the press path. That asymmetry is the entire design.

- [ ] **Step 6: Lift the GT911 sample throttle that would defeat all of this**

`shared/CrowPanelShared/DisplayBringup.cpp:97` throttles GT911 sampling to a
hardcoded 8 ms:

```cpp
  if (touchSampled && now - lastTouchSampleMs < 8) {
```

Every caller of `touchPoints()` gets cached data inside that window, so polling
at `STICK_POLL_MS = 2` would return the same sample four times and buy nothing.
The GT911 itself reports at 100 Hz, so a shorter throttle does not invent data —
it catches each fresh report sooner, worth up to ~6 ms.

Make it configurable, keeping the current value as the default so no existing
project changes behaviour. In `shared/CrowPanelShared/AppConfig.h`, next to the
other `CROW_TOUCH_*` defaults:

```cpp
// Minimum gap between real GT911 I2C samples. Callers polling faster than this
// receive the cached sample. 8 ms suits UI work; project 23 lowers it to 2 ms
// because catching a fresh report late is pure added input latency. This gates
// shared-library code, so a project changing it MUST pass -D.
#ifndef CROW_TOUCH_SAMPLE_MS
#define CROW_TOUCH_SAMPLE_MS 8
#endif
```

Then in `DisplayBringup.cpp:97` replace the literal:

```cpp
  if (touchSampled && now - lastTouchSampleMs < CROW_TOUCH_SAMPLE_MS) {
```

Add `-DCROW_TOUCH_SAMPLE_MS=2` to project 23's build flags everywhere they
appear from here on.

- [ ] **Step 7: Verify no existing project regressed**

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Expected: all green. The default is unchanged at 8, so behaviour is identical
for projects 05/08/10/18/21/22.

- [ ] **Step 8: Compile**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: green.

- [ ] **Step 9: Commit**

```bash
git add in-progress/23-cypher-stick/src/StickTouch.h in-progress/23-cypher-stick/src/StickTouch.cpp shared/CrowPanelShared/AppConfig.h shared/CrowPanelShared/DisplayBringup.cpp
git commit -m "feat(23): fork KeysTouch into latency-retuned StickTouch

Also makes the GT911 sample throttle configurable (CROW_TOUCH_SAMPLE_MS,
default unchanged at 8 ms) — at the hardcoded 8 ms a 2 ms poll returned
the same cached sample four times."
```

---

## Task 7: `StickEngine` — the hot loop, single-threaded first

Build it correct on one core before splitting. The split is Task 8.

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickEngine.h`
- Create: `in-progress/23-cypher-stick/src/StickEngine.cpp`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

- [ ] **Step 1: Write the header**

```cpp
#ifndef CYPHER_STICK_STICK_ENGINE_H
#define CYPHER_STICK_STICK_ENGINE_H

#include "../config/ProjectConfig.h"
#include "SocdCleaner.h"
#include "StickLayout.h"
#include "StickTouch.h"

#include <CrowPanelShared.h>

// The latency-critical path: contacts -> hit-test -> SOCD -> one HID report.
//
// poll() allocates nothing, formats nothing, and draws nothing. Once Task 8
// pins it to core 1 those are hard requirements, not style preferences.
class StickEngine {
 public:
  void begin(HidBackend *hid, StickProfile *profile);

  // One iteration of the input path. Safe to call as fast as you like; the
  // touch layer rate-limits the actual I2C read.
  void poll();

  // Diagnostics, read from the render side. Plain scalars so reading them from
  // another core is safe without a lock.
  uint32_t polls() const { return polls_; }
  uint32_t sends() const { return sends_; }
  uint8_t hat() const { return hat_; }
  uint32_t buttons() const { return buttons_; }
  // Per-key physical touch, independent of SOCD. The renderer highlights from
  // THIS, not from hat/buttons: under up-priority, Left+Right resolves the hat
  // to centre, and highlighting from the hat would leave both keys dark while
  // two fingers are visibly on the glass.
  uint32_t keysHeld() const { return keysHeld_; }
  uint32_t worstPollUs() const { return worstPollUs_; }
  void resetBench() { worstPollUs_ = 0; }

  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

 private:
  HidBackend *hid_ = nullptr;
  StickProfile *profile_ = nullptr;
  StickTouch touch_;
  SocdMemory socd_;
  bool enabled_ = true;
  uint32_t polls_ = 0;
  uint32_t sends_ = 0;
  uint8_t hat_ = 0;
  uint32_t buttons_ = 0;
  uint32_t keysHeld_ = 0;
  uint32_t worstPollUs_ = 0;
};

#endif
```

- [ ] **Step 2: Implement**

```cpp
#include "StickEngine.h"

static_assert(STICK_MAX_KEYS == STICK_LAYOUT_MAX_KEYS,
              "STICK_MAX_KEYS and STICK_LAYOUT_MAX_KEYS must agree");
static_assert(STICK_MAX_PROFILES == STICK_LAYOUT_MAX_PROFILES,
              "STICK_MAX_PROFILES and STICK_LAYOUT_MAX_PROFILES must agree");

void StickEngine::begin(HidBackend *hid, StickProfile *profile) {
  hid_ = hid;
  profile_ = profile;
}

void StickEngine::poll() {
  if (!hid_ || !profile_) return;
  const uint32_t startUs = micros();

  touch_.tick();

  int hits[StickTouch::kMaxContacts];
  int hitCount = 0;
  if (enabled_) {
    for (uint8_t i = 0; i < StickTouch::kMaxContacts; i++) {
      const StickTouch::Contact &c = touch_.contact(i);
      if (!c.active) continue;
      // A contact landing outside every key returns -1 and is dropped by
      // stickResolve. That is the whole of our palm handling.
      hits[hitCount++] = stickHitTest(*profile_, c.x, c.y);
    }
  }

  const StickState s = stickResolve(*profile_, hits, hitCount);

  // socdResolve() MUST be called exactly once per input frame: the Last/First
  // policies infer press order from the transition between calls, so a second
  // call for the same frame sees prev == current and silently mis-resolves,
  // with no error anywhere. Anything else that wants the current direction
  // (a debug overlay, the renderer) reads hat_ below — it never re-calls.
  // socd_ is owned by this task alone and is not thread-safe.
  const uint8_t hat = socdResolve(s.up, s.down, s.left, s.right,
                                  profile_->socdPolicy, socd_);

  keysHeld_ = s.keysHeld;  // physical touch, for the renderer — not SOCD-filtered
  if (hat != hat_ || s.buttons != buttons_) {
    hat_ = hat;
    buttons_ = s.buttons;
    sends_++;
  }
  // Called unconditionally; HidBackend does its own change detection.
  hid_->gamepadState(hat, s.buttons);

  polls_++;
  const uint32_t elapsed = micros() - startUs;
  if (elapsed > worstPollUs_) worstPollUs_ = elapsed;
}
```

- [ ] **Step 3: Wire into the sketch**

Rewrite `23-cypher-stick.ino`, keeping definition-before-use:

```cpp
#include "config/ProjectConfig.h"
#include "src/StickEngine.h"
#include "src/StickLayout.h"

#include <CrowPanelShared.h>

static EventLog gEvents;
static SerialCommandRouter gRouter;
static HidBackend gHid;
static StickProfile gProfile;
static StickEngine gEngine;

static const char *hatName(uint8_t hat) {
  switch (hat) {
    case kHatUp: return "U";
    case kHatUpRight: return "UR";
    case kHatRight: return "R";
    case kHatDownRight: return "DR";
    case kHatDown: return "D";
    case kHatDownLeft: return "DL";
    case kHatLeft: return "L";
    case kHatUpLeft: return "UL";
    default: return "-";
  }
}

static void cmdStatus(const String &args) {
  (void)args;
  Serial.print("mode=");
  Serial.print(gHid.gamepadLive() ? "LIVE" : "MOCK");
  Serial.print(" profile=");
  Serial.print(gProfile.name);
  Serial.print(" keys=");
  Serial.print(gProfile.keyCount);
  Serial.print(" hat=");
  Serial.print(hatName(gEngine.hat()));
  Serial.print(" buttons=0x");
  Serial.print(gEngine.buttons(), HEX);
  Serial.print(" polls=");
  Serial.print(gEngine.polls());
  Serial.print(" sends=");
  Serial.println(gEngine.sends());
}

static void cmdBench(const String &args) {
  (void)args;
  Serial.print("worst poll (touch->send, OUR half only) = ");
  Serial.print(gEngine.worstPollUs());
  Serial.println(" us");
  Serial.println("NOTE: excludes the GT911's own sense+report time (~10 ms at 100 Hz).");
  gEngine.resetBench();
}

// CLAUDE.md: every sketch answers help, status, and history. begin() registers
// help itself; status and history are ours.
static void cmdHistory(const String &args) {
  (void)args;
  gEvents.printHistory(Serial);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  gRouter.begin(Serial, "Cypher Stick");
  gRouter.on("status", "stick status", cmdStatus);
  gRouter.on("bench", "worst observed poll time", cmdBench);
  gRouter.on("history", "recent events", cmdHistory);

  stickDefaultProfile(gProfile);

  // manualFlush=true: Arduino_GFX otherwise cache-syncs on every primitive.
  // With it off we sync once per changed key via CrowDisplay::flush(x,y,w,h),
  // which is exactly the "single-key feedback" hot path the API documents.
  CrowDisplay::begin(activeHardwareProfile(), "Cypher Stick", true);

  gHid.begin(&Serial, &gEvents, "Cypher Stick", "cypherstick");
  gEngine.begin(&gHid, &gProfile);
  gEvents.add("stick ready");

  Serial.println("Cypher Stick ready");
}

void loop() {
  gEngine.poll();
  gRouter.poll();
}
```

`SerialCommandRouter::begin()` registers `help` itself — do not add a `cmdHelp`.

- [ ] **Step 4: Compile both mock and live**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-live --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: both green, and the second with **no** `#warning` about USBMode.

- [ ] **Step 5: Verify real linkage**

```bash
ls _arduino-build/23-cypher-stick-live/libraries/
```

Expected: the USB library directory is present. A green compile alone does not prove linkage.

- [ ] **Step 6: Commit**

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): add StickEngine input path and serial diagnostics"
```

---

## Task 8: Pin the input path to core 1

**Files:**
- Modify: `in-progress/23-cypher-stick/src/StickEngine.h`
- Modify: `in-progress/23-cypher-stick/src/StickEngine.cpp`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

- [ ] **Step 1: Add the task API to the header**

Add to the public section:

```cpp
  // Start the dedicated input task on core 1. After this, do NOT call poll()
  // from loop() — the task owns it. Rendering stays on core 0 because one DSI
  // redraw costs tens of milliseconds and would otherwise stall input.
  void startTask();
```

Add to private:

```cpp
  static void taskEntry(void *arg);
  TaskHandle_t task_ = nullptr;
```

- [ ] **Step 2: Implement**

Add to `StickEngine.cpp`:

```cpp
void StickEngine::taskEntry(void *arg) {
  StickEngine *self = static_cast<StickEngine *>(arg);
  const TickType_t period = pdMS_TO_TICKS(STICK_POLL_MS) > 0
                                ? pdMS_TO_TICKS(STICK_POLL_MS)
                                : 1;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    self->poll();
    vTaskDelayUntil(&last, period);
  }
}

void StickEngine::startTask() {
  if (task_) return;
  // Priority above the Arduino loop task (priority 1) so a long redraw on
  // core 0 can never delay an input poll. Core 1 is the Arduino core on this
  // target; pinning is what guarantees the separation.
  xTaskCreatePinnedToCore(taskEntry, "stick", 4096, this, 5, &task_, 1);
}
```

- [ ] **Step 3: Update the sketch**

In `setup()`, after `gEngine.begin(...)`:

```cpp
  gEngine.startTask();
```

And change `loop()` to stop polling directly:

```cpp
void loop() {
  // The input path runs on its own core-1 task (see StickEngine::startTask).
  // loop() owns rendering, serial, SD, and audio only.
  gRouter.poll();
}
```

- [ ] **Step 4: Compile**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-live --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: green.

- [ ] **Step 5: Commit**

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): pin input path to core 1, leaving core 0 for rendering"
```

---

## Task 9: Profile persistence on SD

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickProfiles.h`
- Create: `in-progress/23-cypher-stick/src/StickProfiles.cpp`
- Modify: `in-progress/23-cypher-stick/test/host_main.cpp`
- Modify: `scripts/test-cypher-stick.sh`
- Modify: `in-progress/23-cypher-stick/config/ProjectConfig.h`

- [ ] **Step 1: Add the flag**

In `config/ProjectConfig.h`, before the `#include <AppConfig.h>`:

```cpp
// SD-backed layout profiles. Off by default (mock-first): with it off the
// default profile is always used and nothing is persisted.
#ifndef USE_STICK_SD
#define USE_STICK_SD 0
#endif
```

- [ ] **Step 2: Write the header**

The serialisation half is Arduino-free so it host-tests; the SD half is not.

```cpp
#ifndef CYPHER_STICK_STICK_PROFILES_H
#define CYPHER_STICK_STICK_PROFILES_H

#include <stdint.h>

#include "StickLayout.h"

// On-card format. Bump kProfileFormatVersion whenever StickKey or StickProfile
// changes shape; a mismatched version is rejected rather than misread.
static const uint32_t kProfileMagic = 0x4B435453;  // "STCK"
static const uint16_t kProfileFormatVersion = 1;

struct ProfileFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t keyCount;
  uint8_t socdPolicy;
  char name[16];
  uint8_t reserved[7];
};

// Serialise `p` into `buf`. Returns bytes written, or 0 if `cap` is too small.
uint32_t stickProfileSerialize(const StickProfile &p, uint8_t *buf, uint32_t cap);

// Parse `buf` into `p`. Returns true on success; false on bad magic, wrong
// version, a key count past STICK_LAYOUT_MAX_KEYS, or a short buffer.
bool stickProfileDeserialize(const uint8_t *buf, uint32_t len, StickProfile &p);

// Bytes a serialised profile occupies.
uint32_t stickProfileSize(const StickProfile &p);

// Re-establish every invariant the in-memory model relies on but the on-card
// bytes cannot be trusted to hold: clamp keyCount to STICK_LAYOUT_MAX_KEYS,
// clamp socdPolicy, and NUL-terminate every key label and the profile name.
//
// This exists because a label is passed straight to Widgets::text(), which
// takes a `const char *` and reads until a terminator — an unterminated label
// from a corrupt or hand-edited card would read past the key struct inside the
// renderer. Call this on every deserialised profile before it is used.
void stickProfileSanitize(StickProfile &p);

#endif
```

- [ ] **Step 3: Write the failing tests**

Add the include to `test/host_main.cpp`:

```cpp
#include "../src/StickProfiles.h"
```

Add before `main()`:

```cpp
static void testProfileRoundTrip() {
  std::printf("profiles: serialise / deserialise round trip\n");
  StickProfile in;
  stickDefaultProfile(in);
  in.socdPolicy = kSocdLastInput;

  uint8_t buf[2048];
  const uint32_t n = stickProfileSerialize(in, buf, sizeof buf);
  check(n > 0, "serialise succeeded");
  check(n == stickProfileSize(in), "reported size matches bytes written");

  StickProfile out;
  check(stickProfileDeserialize(buf, n, out), "deserialise succeeded");
  check(out.keyCount == in.keyCount, "key count survived");
  check(out.socdPolicy == in.socdPolicy, "socd policy survived");
  check(strcmp(out.name, in.name) == 0, "profile name survived");
  // memcmp the whole key rather than spot-checking x/y/bind: w, h, shape,
  // color and key are just as load-bearing, and a field-by-field check only
  // ever pins the fields someone remembered to list.
  for (uint8_t i = 0; i < in.keyCount; i++) {
    char d[64];
    std::snprintf(d, sizeof d, "key %u", i);
    check(memcmp(&out.keys[i], &in.keys[i], sizeof(StickKey)) == 0,
          "key survived byte for byte", d);
  }
}

static void testProfileRejectsBadPolicy() {
  std::printf("profiles: an out-of-range SOCD policy is rejected\n");
  StickProfile in;
  stickDefaultProfile(in);
  uint8_t buf[2048];
  const uint32_t n = stickProfileSerialize(in, buf, sizeof buf);
  // socdPolicy sits at offset 8 (magic 4 + version 2 + keyCount 2).
  buf[8] = 99;
  StickProfile out;
  check(!stickProfileDeserialize(buf, n, out), "policy 99 rejected");
}

static void testProfileEmptyRoundTrip() {
  std::printf("profiles: a zero-key profile round-trips\n");
  StickProfile in;
  memset(&in, 0, sizeof in);
  strncpy(in.name, "Empty", sizeof in.name - 1);
  in.keyCount = 0;
  in.socdPolicy = kSocdNeutral;

  uint8_t buf[2048];
  const uint32_t n = stickProfileSerialize(in, buf, sizeof buf);
  check(n == sizeof(ProfileFileHeader), "header only, no key payload");
  StickProfile out;
  check(stickProfileDeserialize(buf, n, out), "empty profile deserialises");
  check(out.keyCount == 0, "still zero keys");
  check(strcmp(out.name, "Empty") == 0, "name survived");
}

static void testProfileRejectsGarbage() {
  std::printf("profiles: corrupt input is rejected, not misread\n");
  StickProfile in;
  stickDefaultProfile(in);
  uint8_t buf[2048];
  const uint32_t n = stickProfileSerialize(in, buf, sizeof buf);

  StickProfile out;
  check(!stickProfileDeserialize(buf, n / 2, out), "truncated buffer rejected");

  uint8_t bad[2048];
  for (uint32_t i = 0; i < n; i++) bad[i] = buf[i];
  bad[0] ^= 0xFF;  // break the magic
  check(!stickProfileDeserialize(bad, n, out), "bad magic rejected");

  for (uint32_t i = 0; i < n; i++) bad[i] = buf[i];
  bad[4] = 99;  // wrong version
  check(!stickProfileDeserialize(bad, n, out), "wrong version rejected");

  for (uint32_t i = 0; i < n; i++) bad[i] = buf[i];
  bad[6] = 200;  // key count past the maximum
  check(!stickProfileDeserialize(bad, n, out), "absurd key count rejected");
}

static void testProfileTooSmallBuffer() {
  std::printf("profiles: undersized buffer returns 0\n");
  StickProfile in;
  stickDefaultProfile(in);
  uint8_t tiny[8];
  check(stickProfileSerialize(in, tiny, sizeof tiny) == 0, "returns 0");
}
```

Call them in `main()`:

```cpp
  testProfileRoundTrip();
  testProfileRejectsGarbage();
  testProfileTooSmallBuffer();
  testProfileRejectsBadPolicy();
  testProfileEmptyRoundTrip();
```

- [ ] **Step 4: Add the source to the test script**

In `scripts/test-cypher-stick.sh` add:

```bash
  "$PROJECT/src/StickProfiles.cpp" \
```

- [ ] **Step 5: Run to verify it fails**

```bash
./scripts/test-cypher-stick.sh
```

Expected: FAIL — `StickProfiles.cpp` does not exist.

- [ ] **Step 6: Implement**

```cpp
#include "StickProfiles.h"

#include <string.h>

uint32_t stickProfileSize(const StickProfile &p) {
  return (uint32_t)sizeof(ProfileFileHeader) + (uint32_t)p.keyCount * sizeof(StickKey);
}

uint32_t stickProfileSerialize(const StickProfile &p, uint8_t *buf, uint32_t cap) {
  const uint32_t need = stickProfileSize(p);
  if (!buf || cap < need) return 0;
  if (p.keyCount > STICK_LAYOUT_MAX_KEYS) return 0;

  ProfileFileHeader h;
  memset(&h, 0, sizeof h);
  h.magic = kProfileMagic;
  h.version = kProfileFormatVersion;
  h.keyCount = p.keyCount;
  h.socdPolicy = p.socdPolicy;
  memcpy(h.name, p.name, sizeof h.name);

  memcpy(buf, &h, sizeof h);
  memcpy(buf + sizeof h, p.keys, (size_t)p.keyCount * sizeof(StickKey));
  return need;
}

bool stickProfileDeserialize(const uint8_t *buf, uint32_t len, StickProfile &p) {
  if (!buf || len < sizeof(ProfileFileHeader)) return false;

  ProfileFileHeader h;
  memcpy(&h, buf, sizeof h);
  if (h.magic != kProfileMagic) return false;
  if (h.version != kProfileFormatVersion) return false;
  if (h.keyCount > STICK_LAYOUT_MAX_KEYS) return false;
  if (h.socdPolicy > kSocdUpPriority) return false;

  const uint32_t need = (uint32_t)sizeof h + (uint32_t)h.keyCount * sizeof(StickKey);
  if (len < need) return false;

  memset(&p, 0, sizeof p);
  memcpy(p.name, h.name, sizeof p.name);
  p.keyCount = (uint8_t)h.keyCount;
  p.socdPolicy = h.socdPolicy;
  memcpy(p.keys, buf + sizeof h, (size_t)h.keyCount * sizeof(StickKey));
  stickProfileSanitize(p);  // labels from the card are not trusted to terminate
  return true;
}
```

And in `StickLayout.cpp`, since that is where the struct's invariants live:

```cpp
void stickProfileSanitize(StickProfile &p) {
  if (p.keyCount > STICK_LAYOUT_MAX_KEYS) p.keyCount = STICK_LAYOUT_MAX_KEYS;
  if (p.socdPolicy > kSocdUpPriority) p.socdPolicy = kSocdNeutral;
  p.name[sizeof p.name - 1] = '\0';
  for (uint8_t i = 0; i < p.keyCount; i++) {
    p.keys[i].label[sizeof p.keys[i].label - 1] = '\0';
  }
}
```

Add a test that a profile whose label bytes are all non-NUL comes back
terminated, and that a `keyCount` of 200 is clamped rather than trusted.

- [ ] **Step 7: Run to verify it passes**

```bash
./scripts/test-cypher-stick.sh
```

Expected: `N checks, 0 failures`.

- [ ] **Step 8: Commit**

```bash
git add in-progress/23-cypher-stick scripts/test-cypher-stick.sh
git commit -m "feat(23): add versioned profile serialisation with host tests"
```

---

## Task 10: SD load/save and profile serial commands

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickStorage.h`
- Create: `in-progress/23-cypher-stick/src/StickStorage.cpp`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

- [ ] **Step 1: Write the header**

```cpp
#ifndef CYPHER_STICK_STICK_STORAGE_H
#define CYPHER_STICK_STICK_STORAGE_H

#include "../config/ProjectConfig.h"
#include "StickLayout.h"

#include <Arduino.h>

// SD-backed profile storage under /stick/. Degrades to a no-op returning false
// when USE_STICK_SD=0 or no card mounts, so the default profile always works.
//
// NOTE: Arduino FS prepends the mount point and C stdio does not. This class
// uses the Arduino FS API exclusively — do not mix in fopen() paths.
class StickStorage {
 public:
  bool begin();
  bool mounted() const { return mounted_; }

  bool save(const StickProfile &p, const char *name);
  bool load(StickProfile &p, const char *name);
  // Writes up to `cap` names into `out`; returns how many were found.
  uint8_t list(char out[][16], uint8_t cap);

 private:
  bool mounted_ = false;
};

#endif
```

- [ ] **Step 2: Implement**

```cpp
#include "StickStorage.h"

#include "StickProfiles.h"

#if USE_STICK_SD
#include <SD_MMC.h>
#endif

bool StickStorage::begin() {
#if USE_STICK_SD
  // Mount SD BEFORE display init elsewhere in setup(); project 15 hung
  // indefinitely doing it the other way round.
  if (!SD_MMC.begin("/sdcard", true)) return false;
  if (!SD_MMC.exists("/stick")) SD_MMC.mkdir("/stick");
  mounted_ = true;
  return true;
#else
  return false;
#endif
}

bool StickStorage::save(const StickProfile &p, const char *name) {
#if USE_STICK_SD
  if (!mounted_ || !name) return false;
  char path[48];
  snprintf(path, sizeof path, "/stick/%.15s.prf", name);
  static uint8_t buf[sizeof(ProfileFileHeader) + STICK_LAYOUT_MAX_KEYS * sizeof(StickKey)];
  const uint32_t n = stickProfileSerialize(p, buf, sizeof buf);
  if (n == 0) return false;
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  const size_t wrote = f.write(buf, n);
  f.close();
  return wrote == n;
#else
  (void)p; (void)name;
  return false;
#endif
}

bool StickStorage::load(StickProfile &p, const char *name) {
#if USE_STICK_SD
  if (!mounted_ || !name) return false;
  char path[48];
  snprintf(path, sizeof path, "/stick/%.15s.prf", name);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  static uint8_t buf[sizeof(ProfileFileHeader) + STICK_LAYOUT_MAX_KEYS * sizeof(StickKey)];
  const size_t n = f.read(buf, sizeof buf);
  f.close();
  return stickProfileDeserialize(buf, (uint32_t)n, p);
#else
  (void)p; (void)name;
  return false;
#endif
}

uint8_t StickStorage::list(char out[][16], uint8_t cap) {
#if USE_STICK_SD
  if (!mounted_) return 0;
  File dir = SD_MMC.open("/stick");
  if (!dir) return 0;
  uint8_t n = 0;
  for (File e = dir.openNextFile(); e && n < cap; e = dir.openNextFile()) {
    const char *base = strrchr(e.name(), '/');
    base = base ? base + 1 : e.name();
    if (strstr(base, ".prf")) {
      strncpy(out[n], base, 15);
      out[n][15] = '\0';
      char *dot = strstr(out[n], ".prf");
      if (dot) *dot = '\0';
      n++;
    }
    e.close();
  }
  dir.close();
  return n;
#else
  (void)out; (void)cap;
  return 0;
#endif
}
```

- [ ] **Step 3: Add serial commands**

In the sketch, add these **above** `setup()` (definition before use), plus a `static StickStorage gStorage;` global:

```cpp
static void cmdSave(const String &args) {
  const String name = args.length() ? args : String(gProfile.name);
  Serial.println(gStorage.save(gProfile, name.c_str()) ? "saved" : "save failed");
}

static void cmdLoad(const String &args) {
  if (!args.length()) {
    Serial.println("usage: load <name>");
    return;
  }
  StickProfile next;
  if (!gStorage.load(next, args.c_str())) {
    Serial.println("load failed");
    return;
  }
  gEngine.setEnabled(false);
  gProfile = next;
  gEngine.setEnabled(true);
  Serial.print("loaded ");
  Serial.println(gProfile.name);
}

static void cmdProfiles(const String &args) {
  (void)args;
  char names[STICK_MAX_PROFILES][16];
  const uint8_t n = gStorage.list(names, STICK_MAX_PROFILES);
  if (!n) {
    Serial.println("(no profiles — SD off or card empty)");
    return;
  }
  for (uint8_t i = 0; i < n; i++) Serial.println(names[i]);
}
```

Register them in `setup()`:

```cpp
  gRouter.on("save", "save profile to SD", cmdSave);
  gRouter.on("load", "load profile from SD", cmdLoad);
  gRouter.on("profiles", "list SD profiles", cmdProfiles);
```

And call `gStorage.begin();` in `setup()` **before** any display init.

- [ ] **Step 4: Compile with SD on**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-sd --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DUSE_STICK_SD=1" in-progress/23-cypher-stick
```

Expected: green.

- [ ] **Step 5: Commit**

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): add SD profile storage and profile serial commands"
```

---

## Task 11: PLAY rendering

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickRender.h`
- Create: `in-progress/23-cypher-stick/src/StickRender.cpp`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

Runs entirely on core 0. Redraws only keys whose pressed state changed — a full-screen clear-then-redraw is what causes tearing on this single-framebuffer panel.

- [ ] **Step 1: Write the header**

```cpp
#ifndef CYPHER_STICK_STICK_RENDER_H
#define CYPHER_STICK_STICK_RENDER_H

#include "../config/ProjectConfig.h"
#include "StickLayout.h"

#include <Arduino.h>

// PLAY-view rendering. Core 0 only.
//
// Delta-draws: only keys whose pressed state changed are repainted. The DSI
// panel is single-framebuffer, so clearing and redrawing everything tears.
class StickRender {
 public:
  void begin(const StickProfile *profile);
  // Repaint everything (mode change, profile change).
  void drawAll();
  // Repaint only what changed since the last call. `keysHeld` is StickEngine's
  // per-key physical-touch mask; reading it across cores is safe because it is
  // a scalar.
  //
  // It is deliberately NOT derived from hat/buttons. Inverting those back to
  // per-key state is lossy: keys sharing a bind, kBindNone keys, and — on the
  // DEFAULT profile — Left+Right under up-priority, which resolves the hat to
  // centre and would leave both keys dark with two fingers on the glass.
  void update(uint32_t keysHeld);

 private:
  void drawKey(uint8_t idx, bool pressed);

  const StickProfile *profile_ = nullptr;
  uint32_t lastMask_ = 0;  // bit i = key i was drawn pressed
  bool drawnOnce_ = false;
};

#endif
```

- [ ] **Step 2: Implement**

```cpp
#include "StickRender.h"

#include "SocdCleaner.h"

#include <CrowPanelShared.h>

void StickRender::begin(const StickProfile *profile) {
  profile_ = profile;
  lastMask_ = 0;
  drawnOnce_ = false;
}

void StickRender::drawKey(uint8_t idx, bool pressed) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // NOTE: the accessor is canvas(), not gfx(). Colours come from the RGB565
  // Widgets palette — the UiTheme struct holds uint32_t values for a different
  // purpose and is not what Arduino_GFX wants.
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const StickKey &k = profile_->keys[idx];
  const uint16_t fill = pressed ? Widgets::kAccent : Widgets::kSurface;
  const uint16_t edge = pressed ? Widgets::kTextHi : Widgets::kLine;

  if (k.shape == kShapeRound) {
    const int16_t r = (k.w < k.h ? k.w : k.h) / 2;
    g->fillCircle(k.x + k.w / 2, k.y + k.h / 2, r, fill);
    g->drawCircle(k.x + k.w / 2, k.y + k.h / 2, r, edge);
  } else {
    g->fillRoundRect(k.x, k.y, k.w, k.h, 10, fill);
    g->drawRoundRect(k.x, k.y, k.w, k.h, 10, edge);
  }

  Widgets::text(g, k.x + k.w / 2, k.y + k.h / 2 - 8, k.label, Widgets::fontS(),
                Widgets::kTextHi, Widgets::kCenter);

  // Sync only this key's rectangle. begin(..., manualFlush=true) turned off
  // per-primitive cache syncing precisely so this is one sync, not dozens.
  CrowDisplay::flush(k.x - 2, k.y - 2, k.w + 4, k.h + 4);
#else
  (void)idx;
  (void)pressed;
#endif
}

void StickRender::drawAll() {
  if (!profile_) return;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (g) g->fillScreen(Widgets::kBg);
#endif
  for (uint8_t i = 0; i < profile_->keyCount; i++) {
    drawKey(i, (lastMask_ >> i) & 1u);
  }
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::flush();  // full-screen sync, once
#endif
  drawnOnce_ = true;
}

void StickRender::update(uint32_t keysHeld) {
  if (!profile_) return;
  if (!drawnOnce_) {
    drawAll();
    return;
  }
  // StickEngine already computed which keys are physically held; do not try to
  // reconstruct it from hat/buttons (see the header for why that is lossy).
  const uint32_t mask = keysHeld;
  const uint32_t changed = mask ^ lastMask_;
  if (!changed) return;
  for (uint8_t i = 0; i < profile_->keyCount; i++) {
    if (changed & (1u << i)) drawKey(i, (mask >> i) & 1u);
  }
  lastMask_ = mask;
}
```

- [ ] **Step 3: Wire into the sketch**

Add `static StickRender gRender;` as a global. In `setup()` after display bringup:

```cpp
  gRender.begin(&gProfile);
  gRender.drawAll();
```

In `loop()`:

```cpp
void loop() {
  gRender.update(gEngine.keysHeld());
  gRouter.poll();
}
```

- [ ] **Step 4: Compile**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-live --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: green.

If `UiTheme::accentBright()` does not exist, check `shared/CrowPanelShared/UiTheme.h` and substitute the nearest available accent colour rather than inventing one.

- [ ] **Step 5: Commit**

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): add delta-drawing PLAY view"
```

---

## Task 12: Layout editor

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickEditor.h`
- Create: `in-progress/23-cypher-stick/src/StickEditor.cpp`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

- [ ] **Step 1: Write the header**

```cpp
#ifndef CYPHER_STICK_STICK_EDITOR_H
#define CYPHER_STICK_STICK_EDITOR_H

#include "../config/ProjectConfig.h"
#include "StickLayout.h"
#include "StickTouch.h"

#include <Arduino.h>

// Drag / resize / rebind UI. Core 0 only.
//
// The engine is disabled while this is open: sending gamepad reports to a host
// while the operator is dragging keys around would fire inputs into whatever
// game has focus.
class StickEditor {
 public:
  void begin(StickProfile *profile);
  void open();
  void close();
  bool isOpen() const { return open_; }

  // Feed the primary contact each loop. Returns true if the layout changed.
  bool tick(bool down, int16_t x, int16_t y);

  int selected() const { return selected_; }
  void nudgeSize(int16_t delta);
  void cycleBind();

 private:
  void draw();
  void drawKeyChrome(uint8_t idx, bool selected);

  StickProfile *profile_ = nullptr;
  bool open_ = false;
  bool dragging_ = false;
  int selected_ = -1;
  int16_t grabDx_ = 0, grabDy_ = 0;
  bool wasDown_ = false;
};

#endif
```

- [ ] **Step 2: Implement the interaction core**

```cpp
#include "StickEditor.h"

#include <CrowPanelShared.h>

void StickEditor::begin(StickProfile *profile) { profile_ = profile; }

void StickEditor::open() {
  open_ = true;
  selected_ = -1;
  dragging_ = false;
  draw();
}

void StickEditor::close() { open_ = false; }

bool StickEditor::tick(bool down, int16_t x, int16_t y) {
  if (!open_ || !profile_) return false;
  bool changed = false;

  if (down && !wasDown_) {
    // Press: pick up whatever key is under the finger.
    const int hit = stickHitTest(*profile_, x, y);
    if (hit >= 0) {
      selected_ = hit;
      dragging_ = true;
      grabDx_ = x - profile_->keys[hit].x;
      grabDy_ = y - profile_->keys[hit].y;
    } else {
      selected_ = -1;
    }
    draw();
  } else if (down && dragging_ && selected_ >= 0) {
    StickKey &k = profile_->keys[selected_];
    const int16_t nx = x - grabDx_;
    const int16_t ny = y - grabDy_;
    if (nx != k.x || ny != k.y) {
      // Clamp to the panel so a key can never be dragged out of reach.
      k.x = nx < 0 ? 0 : (nx + k.w > 1024 ? (int16_t)(1024 - k.w) : nx);
      k.y = ny < 0 ? 0 : (ny + k.h > 600 ? (int16_t)(600 - k.h) : ny);
      changed = true;
      draw();
    }
  } else if (!down && wasDown_) {
    dragging_ = false;
  }

  wasDown_ = down;
  return changed;
}

void StickEditor::nudgeSize(int16_t delta) {
  if (selected_ < 0 || !profile_) return;
  StickKey &k = profile_->keys[selected_];
  int16_t w = k.w + delta;
  if (w < 40) w = 40;
  if (w > 200) w = 200;
  k.w = w;
  k.h = w;
  draw();
}

void StickEditor::cycleBind() {
  if (selected_ < 0 || !profile_) return;
  StickKey &k = profile_->keys[selected_];
  // Order: Up, Down, Left, Right, then every button, then wrap.
  //
  // Uses the shared predicates from StickLayout.h rather than open-coding the
  // bounds. An earlier draft wrapped at kBindButton0 + 7, which meant a profile
  // loaded from SD with button 12 bound fell through every branch and jumped to
  // kBindUp, silently making buttons 8..31 unreachable from the editor.
  if (stickBindIsDirection(k.bind)) {
    k.bind = (k.bind == kBindRight) ? kBindButton0 : (uint8_t)(k.bind + 1);
  } else if (stickBindIsButton(k.bind)) {
    const uint8_t next = (uint8_t)(k.bind + 1);
    k.bind = stickBindIsButton(next) ? next : kBindUp;
  } else {
    // Reserved (4..15) or kBindNone: treat as unbound and restart the cycle.
    k.bind = kBindUp;
  }
  draw();
}

void StickEditor::drawKeyChrome(uint8_t idx, bool selected) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const StickKey &k = profile_->keys[idx];
  g->fillRoundRect(k.x, k.y, k.w, k.h, 10, Widgets::kSurface);
  g->drawRoundRect(k.x, k.y, k.w, k.h, 10,
                   selected ? Widgets::kAccent : Widgets::kLine);
  if (selected) {
    g->drawRoundRect(k.x - 2, k.y - 2, k.w + 4, k.h + 4, 12, Widgets::kAccent);
  }
  Widgets::text(g, k.x + 8, k.y + k.h / 2 - 8, k.label, Widgets::fontS(),
                Widgets::kTextHi, Widgets::kLeft);
#else
  (void)idx;
  (void)selected;
#endif
}

void StickEditor::draw() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(Widgets::kBg);
  Widgets::text(g, 16, 12, "EDIT - drag to move, SIZE +/-, BIND to rebind",
                Widgets::fontS(), Widgets::kTextMut, Widgets::kLeft);
  for (uint8_t i = 0; i < profile_->keyCount; i++) {
    drawKeyChrome(i, i == (uint8_t)selected_);
  }
  // The editor redraws whole frames, so one full sync per draw is correct here
  // (unlike the PLAY view, which syncs per changed key).
  CrowDisplay::flush();
#endif
}
```

- [ ] **Step 3: Wire into the sketch**

Add globals `static StickEditor gEditor;` and `static CrowTouch gUiTouch;`. Add these commands above `setup()`:

```cpp
static void cmdEdit(const String &args) {
  (void)args;
  gEngine.setEnabled(false);
  gEditor.open();
  Serial.println("edit mode — input suspended");
}

static void cmdPlay(const String &args) {
  (void)args;
  gEditor.close();
  gRender.drawAll();
  gEngine.setEnabled(true);
  Serial.println("play mode — input live");
}

static void cmdBind(const String &args) {
  (void)args;
  gEditor.cycleBind();
  Serial.println("bind cycled");
}

static void cmdSize(const String &args) {
  gEditor.nudgeSize(args.startsWith("-") ? -8 : 8);
  Serial.println("size nudged");
}
```

Register in `setup()`:

```cpp
  gRouter.on("edit", "open layout editor", cmdEdit);
  gRouter.on("play", "return to play mode", cmdPlay);
  gRouter.on("bind", "cycle selected key binding", cmdBind);
  gRouter.on("size", "nudge selected key size (size -)", cmdSize);
```

Update `loop()`:

```cpp
void loop() {
  gUiTouch.tick();
  if (gEditor.isOpen()) {
    gEditor.tick(gUiTouch.down(), gUiTouch.x(), gUiTouch.y());
  } else {
    gRender.update(gEngine.keysHeld());
  }
  gRouter.poll();
}
```

- [ ] **Step 4: Compile**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-live --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: green.

- [ ] **Step 5: Commit**

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): add on-panel drag-and-drop layout editor"
```

---

## Task 13: SOCD policy command and settings

**Files:**
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

- [ ] **Step 1: Add the command**

Above `setup()`:

```cpp
static void cmdSocd(const String &args) {
  if (args == "neutral") gProfile.socdPolicy = kSocdNeutral;
  else if (args == "last") gProfile.socdPolicy = kSocdLastInput;
  else if (args == "first") gProfile.socdPolicy = kSocdFirstInput;
  else if (args == "up") gProfile.socdPolicy = kSocdUpPriority;
  else {
    Serial.println("usage: socd neutral|last|first|up");
    return;
  }
  Serial.print("socd policy = ");
  Serial.println(args);
}
```

Register:

```cpp
  gRouter.on("socd", "set SOCD policy (neutral|last|first|up)", cmdSocd);
```

- [ ] **Step 2: Compile and commit**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): add SOCD policy serial command"
```

---

## Task 14: Press-click audio

**Files:**
- Create: `in-progress/23-cypher-stick/src/StickAudio.h`
- Create: `in-progress/23-cypher-stick/src/StickAudio.cpp`
- Modify: `in-progress/23-cypher-stick/config/ProjectConfig.h`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

Runs on core 0, driven by the render side's changed-key mask. **Never call this
from the stick task** — it touches I2S and would put a driver call on the
latency path.

- [ ] **Step 1: Add the flag**

In `config/ProjectConfig.h` before `#include <AppConfig.h>`:

```cpp
// Click feedback out of the onboard NS4168 speaker.
#ifndef USE_STICK_AUDIO
#define USE_STICK_AUDIO 0
#endif
```

- [ ] **Step 2: Write the header**

```cpp
#ifndef CYPHER_STICK_STICK_AUDIO_H
#define CYPHER_STICK_STICK_AUDIO_H

#include "../config/ProjectConfig.h"

#include <Arduino.h>

// Short click on key press. Core 0 only — this touches the I2S driver and must
// never run on the stick task.
//
// The amp enable (IO30) is ACTIVE-LOW: driving it HIGH mutes the speaker while
// I2S keeps streaming, which looks exactly like working code that makes no
// sound. Polarity lives in HardwareProfile as controlActiveHigh=false; write it
// through that field, never as a bare HIGH/LOW.
class StickAudio {
 public:
  bool begin();
  void click();          // fire one press click
  void setVolume(uint8_t v);  // 0-255
  uint8_t volume() const { return volume_; }
  bool ready() const { return ready_; }

 private:
  bool ready_ = false;
  uint8_t volume_ = 160;
};

#endif
```

- [ ] **Step 3: Implement**

Model the I2S setup on project 21's `KeyAudio.cpp` — raw `i2s_std`, 16-bit
stereo, no codec init (the NS4168 needs none). Stream a block of silence before
raising the amp enable to avoid a turn-on pop.

```cpp
#include "StickAudio.h"

#include <CrowPanelShared.h>

#if USE_STICK_AUDIO
#include <driver/i2s_std.h>

static i2s_chan_handle_t gTx = nullptr;
static const uint32_t kSampleRate = 16000;
#endif

bool StickAudio::begin() {
#if USE_STICK_AUDIO
  const HardwareProfile &p = activeHardwareProfile();

  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan, &gTx, nullptr) != ESP_OK) return false;

  i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)p.audio.bclk,
      .ws = (gpio_num_t)p.audio.lrclk,
      .dout = (gpio_num_t)p.audio.dout,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = {false, false, false},
    },
  };
  if (i2s_channel_init_std_mode(gTx, &std) != ESP_OK) return false;
  if (i2s_channel_enable(gTx) != ESP_OK) return false;

  // Silence first, then enable the amp — otherwise the speaker pops.
  static int16_t silence[256] = {0};
  size_t wrote = 0;
  i2s_channel_write(gTx, silence, sizeof silence, &wrote, 100);

  pinMode(p.audio.control, OUTPUT);
  digitalWrite(p.audio.control, p.audio.controlActiveHigh ? HIGH : LOW);

  ready_ = true;
  return true;
#else
  return false;
#endif
}

void StickAudio::setVolume(uint8_t v) { volume_ = v; }

void StickAudio::click() {
#if USE_STICK_AUDIO
  if (!ready_) return;
  // A short decaying square burst: audible over game audio, ~6 ms so it cannot
  // stack up if several keys land in the same frame.
  const int frames = kSampleRate * 6 / 1000;
  static int16_t buf[256];
  int done = 0;
  while (done < frames) {
    int n = frames - done;
    if (n > 128) n = 128;
    for (int i = 0; i < n; i++) {
      const int idx = done + i;
      const int16_t amp = (int16_t)((volume_ * 40) * (frames - idx) / frames);
      const int16_t s = ((idx / 8) & 1) ? amp : (int16_t)-amp;
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    size_t wrote = 0;
    i2s_channel_write(gTx, buf, (size_t)n * 4, &wrote, 20);
    done += n;
  }
#endif
}
```

- [ ] **Step 4: Fire it from the render side**

`StickRender::update()` already computes `changed`. Add a click when any key
newly went down. In the sketch, add `static StickAudio gAudio;`, call
`gAudio.begin();` in `setup()`, and give `StickRender::update()` an out-param
or have the sketch compare — simplest is to expose the mask:

Add to `StickRender.h` public:

```cpp
  uint32_t pressedMask() const { return lastMask_; }
```

And in the sketch's `loop()`:

```cpp
static uint32_t gPrevMask = 0;

void loop() {
  gUiTouch.tick();
  if (gEditor.isOpen()) {
    gEditor.tick(gUiTouch.down(), gUiTouch.x(), gUiTouch.y());
  } else {
    gRender.update(gEngine.keysHeld());
    const uint32_t mask = gRender.pressedMask();
    if (mask & ~gPrevMask) gAudio.click();  // something newly went down
    gPrevMask = mask;
  }
  gRouter.poll();
}
```

- [ ] **Step 5: Compile with audio on**

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-audio --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DUSE_STICK_AUDIO=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: green.

- [ ] **Step 6: Commit**

```bash
git add in-progress/23-cypher-stick
git commit -m "feat(23): add press-click audio on the render core"
```

---

## Task 15: Keyboard output mode and the OUT toggle

**Correction to the spec:** it claims keyboard mode is "nearly free since the
code already exists". That is wrong, and the reason matters. Every existing
keyboard path is *tap*-only — `UsbTransport::keyUp()` is
`gKeyboard.releaseAll()`, with no per-key release anywhere. Holding one key
while pressing another is exactly what a fightstick needs and exactly what the
current transport cannot express. This task adds that capability.

The addition is **non-breaking by construction**: the new methods are virtual
with default no-op bodies rather than pure virtual, so `BleTransport` needs no
change and projects 05/21 are untouched.

**Files:**
- Modify: `shared/CrowPanelShared/CrowHidTransport.h`
- Modify: `shared/CrowPanelShared/CrowUsbTransport.h`
- Modify: `shared/CrowPanelShared/CrowUsbTransport.cpp`
- Modify: `shared/CrowPanelShared/CrowHidBackend.{h,cpp}`
- Modify: `in-progress/23-cypher-stick/23-cypher-stick.ino`

- [ ] **Step 1: Extend the transport interface with defaults**

In `CrowHidTransport.h`, add inside `class HidTransport` after `keyUp()`:

```cpp
  // Per-key hold/release for held-input use cases (project 23's fightstick
  // keyboard mode). Default no-ops with supportsHeldKeys()==false, so a
  // transport that cannot express a per-key release simply does not
  // participate and no existing transport needs changing.
  virtual bool supportsHeldKeys() const { return false; }
  virtual void keyPressHeld(uint8_t key) { (void)key; }
  virtual void keyReleaseHeld(uint8_t key) { (void)key; }
```

- [ ] **Step 2: Override them in the USB transport**

In `CrowUsbTransport.h`, add to `class UsbTransport`:

```cpp
  bool supportsHeldKeys() const override { return CROW_HID_USB_LIVE; }
  void keyPressHeld(uint8_t key) override;
  void keyReleaseHeld(uint8_t key) override;
```

In `CrowUsbTransport.cpp`, add:

```cpp
void UsbTransport::keyPressHeld(uint8_t key) {
#if CROW_HID_USB_LIVE
  // press() holds until a matching release(); this is NOT keyDown(), which
  // pairs with a releaseAll().
  if (key) gKeyboard.press(key);
#else
  (void)key;
#endif
}

void UsbTransport::keyReleaseHeld(uint8_t key) {
#if CROW_HID_USB_LIVE
  if (key) gKeyboard.release(key);
#else
  (void)key;
#endif
}
```

- [ ] **Step 3: Add a diffing keyboard-state call to HidBackend**

In `CrowHidBackend.h` public:

```cpp
  // Held-keyboard state for project 23. Presses and releases only the delta
  // against the previous call, so holds persist. Like gamepadState(), this
  // deliberately bypasses tapKey()/kHoldMs.
  void keyboardHeldState(const uint8_t *keys, uint8_t count);
```

Private:

```cpp
  uint8_t heldKeys_[8] = {0};
  uint8_t heldKeyCount_ = 0;
```

In `CrowHidBackend.cpp`:

```cpp
void HidBackend::keyboardHeldState(const uint8_t *keys, uint8_t count) {
  HidTransport *t = active();
  if (!t || !t->supportsHeldKeys()) return;
  if (count > 8) count = 8;

  // Release anything held last time that is not held now.
  for (uint8_t i = 0; i < heldKeyCount_; i++) {
    bool still = false;
    for (uint8_t j = 0; j < count; j++) {
      if (keys[j] == heldKeys_[i]) { still = true; break; }
    }
    if (!still) t->keyReleaseHeld(heldKeys_[i]);
  }
  // Press anything held now that was not held last time.
  for (uint8_t j = 0; j < count; j++) {
    bool had = false;
    for (uint8_t i = 0; i < heldKeyCount_; i++) {
      if (heldKeys_[i] == keys[j]) { had = true; break; }
    }
    if (!had) t->keyPressHeld(keys[j]);
  }

  for (uint8_t j = 0; j < count; j++) heldKeys_[j] = keys[j];
  heldKeyCount_ = count;
}
```

- [ ] **Step 4: Emit keyboard state from the engine**

In `StickEngine.h` add:

```cpp
  enum StickOut : uint8_t { kOutPad = 0, kOutKey = 1 };
  void setOutput(uint8_t out) { out_ = out; }
  uint8_t output() const { return out_; }
```

Private: `uint8_t out_ = kOutPad;`

In `StickEngine::poll()`, replace the single `hid_->gamepadState(...)` call with:

```cpp
  if (out_ == kOutKey) {
    // Collect the keycodes of every held key, directions included.
    uint8_t keys[8];
    uint8_t n = 0;
    for (int i = 0; i < hitCount && n < 8; i++) {
      const int idx = hits[i];
      if (idx < 0 || idx >= (int)profile_->keyCount) continue;
      const uint8_t kc = profile_->keys[idx].key;
      if (kc) keys[n++] = kc;
    }
    hid_->keyboardHeldState(keys, n);
  } else {
    hid_->gamepadState(hat, s.buttons);
  }
```

Note that keyboard mode intentionally bypasses SOCD: the host sees the raw keys,
exactly as a physical keyboard would, and a keyboard is not subject to the hat's
structural cleaning. Say so in `TECHNICAL.md`.

- [ ] **Step 5: Add the OUT command**

Above `setup()`:

```cpp
static void cmdOut(const String &args) {
  if (args == "pad") gEngine.setOutput(StickEngine::kOutPad);
  else if (args == "key") gEngine.setOutput(StickEngine::kOutKey);
  else {
    Serial.println("usage: out pad|key");
    return;
  }
  Serial.print("output = ");
  Serial.println(args);
}
```

Register: `gRouter.on("out", "output mode (pad|key)", cmdOut);`

- [ ] **Step 6: Verify nothing regressed, then compile 23**

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Expected: all green — projects 05 and 21 must be unaffected, since the new
interface methods have default bodies.

```bash
CTAGS_WORKAROUND=1 arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared --build-path _arduino-build/23-cypher-stick-live --build-property tools.ctags.cmd.path=/usr/bin/true --build-property compiler.cpp.extra_flags="-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DUSE_USB_HID=1 -DCROW_TOUCH_SAMPLE_MS=2" in-progress/23-cypher-stick
```

Expected: green.

- [ ] **Step 7: Commit**

```bash
git add shared/CrowPanelShared in-progress/23-cypher-stick
git commit -m "feat(23): add held-key keyboard output mode and OUT toggle

Adds per-key press/release to the transport interface as virtual methods
with default no-op bodies, so BleTransport and projects 05/21 are
unaffected. The existing keyUp() is releaseAll() and cannot express a
held key, which is what a fightstick needs."
```

---

## Task 16: Flag matrix, full build, and documentation

**Files:**
- Modify: `scripts/check-flag-matrix.sh`
- Create: `in-progress/23-cypher-stick/README.md`
- Create: `in-progress/23-cypher-stick/TECHNICAL.md`
- Modify: `docs/full-port-proof-matrix.md`
- Modify: `README.md`
- Modify: `AGENTS.md`

- [ ] **Step 1: Add flag-matrix rows**

In `scripts/check-flag-matrix.sh`, define `P23="in-progress/23-cypher-stick"` alongside the other project variables, then add rows following the existing `"$P21|...` format:

```
  "$P23|baseline|-DUSE_DISPLAY=0|"
  "$P23|display|-DUSE_DISPLAY=1 -DCROW_TOUCH_SAMPLE_MS=2|GFX Library for Arduino,SensorLib"
  "$P23|gamepad-mock|-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DCROW_TOUCH_SAMPLE_MS=2|GFX Library for Arduino,SensorLib"
  "$P23|gamepad-sd|-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DUSE_STICK_SD=1 -DCROW_TOUCH_SAMPLE_MS=2|GFX Library for Arduino,SensorLib"
  "$P23|gamepad-audio|-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DUSE_STICK_AUDIO=1 -DCROW_TOUCH_SAMPLE_MS=2|GFX Library for Arduino,SensorLib"
  "$P23|keyboard-mode|-DUSE_DISPLAY=1 -DUSE_USB_HID=1 -DCROW_TOUCH_SAMPLE_MS=2|GFX Library for Arduino,SensorLib"
  "$P23|full|-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DUSE_USB_HID=1 -DUSE_STICK_SD=1 -DUSE_STICK_AUDIO=1 -DCROW_TOUCH_SAMPLE_MS=2|GFX Library for Arduino,SensorLib"
```

- [ ] **Step 2: Run the full gate**

```bash
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

Expected: every row green, including the pre-existing projects. If a project other than 23 fails, the shared-library change regressed it — fix before continuing.

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

```bash
./scripts/test-cypher-stick.sh
```

Expected: all green.

- [ ] **Step 3: Write the project README**

Create `in-progress/23-cypher-stick/README.md`. It **must** carry the honest limits — this repo's value proposition is that claims match evidence:

```markdown
# Cypher Stick

A touch fightstick for the Elecrow CrowPanel Advanced 7-inch display. Presents
to a PC or Nintendo Switch as a real USB gamepad, with an on-panel
drag-and-drop layout editor and per-game profiles.

> This is Project 23 in the [CrowPanel Arduino suite](../../README.md).

## Status

**Compile-ready.** Every flag combination builds for the ESP32-P4 target and the
SOCD cleaner, hit-testing, and profile serialisation are host-tested. **Nothing
here has run on a real CrowPanel**, and every latency figure below is a
projection from datasheet numbers, not a measurement.

## Honest limits

These are permanent properties of the hardware, not bugs to be fixed later:

- **No tactile edge.** You cannot feel where a button is without looking. This
  is the documented reason touchscreen controllers underperform physical ones,
  and audio clicks plus press flashes do not substitute for it.
- **5 simultaneous inputs, maximum.** The GT911 touch controller tracks five
  contacts. A resting palm consumes one of them.
- **PC and Nintendo Switch only.** PS5 and Xbox require passthrough
  authentication and will never work with this.
- **Projected ~13 ms press latency and ~37 ms release latency**, versus ~1–2 ms
  for both on a physical GP2040-CE leverless — roughly one frame behind on
  press, two on release. The gap is deliberate: this panel's touch controller is
  documented to drop contacts mid-touch, so a release is confirmed over 24 ms
  rather than trusted immediately. A late release is survivable; a phantom one
  costs you a block. Run `bench` on hardware to measure our half; the GT911's
  internal ~10 ms cannot be measured from here.

## Deliberately not included

No turbo, no autofire, no input macros, and no input recording or replay. These
are banned in tournament play. One press produces one input.
```

- [ ] **Step 4: Write `TECHNICAL.md`**

Create `in-progress/23-cypher-stick/TECHNICAL.md` covering: the two FQBNs and why `USBMode=default` is mandatory; the `USE_USB_GAMEPAD` three-layer flag requirement; the `send()`-not-`pressButton()` rule and why; the core-1/core-0 split and the no-heap-no-String rule on the stick task; the SOCD policies; the profile file format and version field; and the full serial command list.

- [ ] **Step 5: Add the proof-matrix row**

In `docs/full-port-proof-matrix.md`, add a row in numeric order:

```
| 23 Cypher Stick | `USE_DISPLAY`, `USE_USB_GAMEPAD` (needs `USBMode=default`), `USE_STICK_SD` | touch fightstick presenting as a native USB gamepad (TinyUSB HID, 32 buttons + 8-way hat) via one atomic `send()` per state change; pure SOCD cleaner (neutral/last/first/up-priority) whose output is a single 0-8 hat, so opposing cardinals are unrepresentable on the wire; multi-contact hit-testing against a draggable per-game layout; input path pinned to core 1 with all rendering on core 0 | **compile-ready.** All flag combos build; host tests cover SOCD resolution (16 combos x 4 policies), hit-testing, and profile round-trip including corrupt input. **Nothing exercised on hardware:** the gamepad has never enumerated on a host, no latency has been measured, and the core split has never run. Latency figures in the project README are datasheet projections, not measurements |
```

Add to the Safety Boundaries section:

```
- Cypher Stick is an operator-driven game controller: no turbo, autofire, input
  macros, or input recording/replay, all of which are banned in tournament play.
```

- [ ] **Step 6: Update the root README and AGENTS.md**

Add project 23 to the in-progress table in `README.md` with its flags, and mirror any new build-command or flag information into `AGENTS.md` — `CLAUDE.md` requires the two stay in sync.

- [ ] **Step 7: Commit**

```bash
git add scripts/check-flag-matrix.sh in-progress/23-cypher-stick README.md AGENTS.md docs/full-port-proof-matrix.md
git commit -m "docs(23): add Cypher Stick docs, proof-matrix row, and flag-matrix gate"
```

---

## Definition of done

- [ ] `./scripts/test-cypher-stick.sh` green
- [ ] `CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh` green for **every** project, not just 23
- [ ] `CTAGS_WORKAROUND=1 ./scripts/compile-all.sh` green — projects 05, 18, 21, 22 must be unaffected by the shared-library changes
- [ ] `_arduino-build/23-cypher-stick-live/libraries/` contains the USB library (linkage verified by inspection, not by a green compile)
- [ ] The `USBMode=default` build emits **no** `#warning` about falling back to MOCK
- [ ] `CROW_TOUCH_SAMPLE_MS` still defaults to `8` in `AppConfig.h`, so no existing project's touch behaviour changed
- [ ] Proof state recorded as `compile-ready` everywhere, with no hardware claims

## Observed on hardware, 2026-08-01 (Task 7 build) — UNRESOLVED

First flash to a real CrowPanel. `USBMode=default` with
`-DUSE_DISPLAY=1 -DUSE_USB_GAMEPAD=1 -DCROW_TOUCH_SAMPLE_MS=2`, 506496 bytes,
hash verified, hard reset.

**Result: backlight on, screen blank, and the device does not enumerate on USB
at all** — no HID gamepad, no CDC serial port.

The two symptoms together suggest `setup()` does not complete. The backlight is
raised inside `CrowDisplay::begin()`, which runs *before* `gHid.begin()` (and
therefore before `USB.begin()`), so a stall in or shortly after display bringup
would produce exactly this: light on, nothing drawn, no USB.

Candidates, in the order worth checking:

1. **`CrowDisplay::begin(..., manualFlush=true)`.** `DisplayBringup.cpp:203`
   does call `flush()` to make the status screen appear, so a blank panel means
   either that path did not run or the panel never finished init. Try
   `manualFlush=false` first — it is a one-word change and isolates the whole
   question.
2. **A hang in display bringup itself.** Project 15 hit an indefinite hang from
   ordering in `setup()` (SD after display); the shape here is similar even if
   the cause is not.
3. **`USB.begin()` never reached.** If 1 or 2 is the cause this follows
   automatically and is not a separate bug.

Do not attribute this to the gamepad path without evidence — the gamepad code
runs after the suspected stall point, so it is more likely a victim than a
cause. **No hardware claim of any kind may be made for this project until this
is understood.**

## First things to check when hardware is available

In order, because each depends on the previous:

1. The host enumerates the panel as a gamepad — verify in a Windows/Linux
   controller test app before anything else.
2. `bench` reports a worst poll time in the expected range.
3. A resting palm does not break input. If it does, that is the GT911 5-contact
   ceiling being consumed, and the fix is the size-byte plumbing deliberately
   left out of this plan.
4. Hold a direction for several seconds and confirm it never releases early —
   this validates dropping the 30 ms debounce to a 1-poll confirm.
5. Only then: measure real end-to-end latency against a physical controller.
