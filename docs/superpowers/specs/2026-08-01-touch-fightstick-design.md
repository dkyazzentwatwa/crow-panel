# Cypher Stick — Touch Fightstick Design

**Date:** 2026-08-01
**Project:** `in-progress/23-cypher-stick`
**Status:** design approved, not yet implemented

## Why this exists

There is no touchscreen fightstick on the market. Sticks *with* a small status
LCD exist; controllers whose buttons *are* the screen do not. The reason is
documented rather than accidental: virtual controls measurably underperform
physical ones because there is no tactile edge to find without looking, and
tactile feedback arriving later than ~30 ms reads to the player as a missed
input.

This project does not pretend to solve that. It takes the one axis where glass
wins outright — relayout in seconds instead of drilling a new panel — and builds
the shortest touch-to-USB path this hardware can support, then states the
remaining gap in measured frames instead of adjectives.

## What it is

A playable leverless-style fightstick on the CrowPanel Advanced 7-inch panel,
presenting to the host as a real USB gamepad, with an on-panel drag-and-drop
layout editor and per-game profiles.

It is **not** a tournament replacement for a physical leverless, and the README
must say so.

## Constraints, measured and fixed

**Press and release have different budgets, and an earlier draft of this spec
wrongly quoted the press figure as if it covered both.**

| Stage | Project 21 | Target here | Fixed by |
|---|---|---|---|
| GT911 sense + report | ~10 ms | ~10 ms | hardware (100 Hz report rate, <5 ms response) |
| Our I2C poll | 16 ms | ~2 ms | `StickTouch` polls every loop — **and** `CROW_TOUCH_SAMPLE_MS`, since `DisplayBringup.cpp:97` throttles real GT911 samples to a hardcoded 8 ms that would otherwise return cached data four polls running |
| USB interrupt endpoint | 1 ms | 1 ms | `TUD_HID_INOUT_DESCRIPTOR(..., 1)` in core 3.3.8 |
| **Press total** | **~27 ms ≈ 1.6 frames** | **~13 ms ≈ 0.8 frames** | |
| Release confirm | 30 ms | 24 ms | **kept deliberately — see below** |
| **Release total** | **~57 ms ≈ 3.4 frames** | **~37 ms ≈ 2.2 frames** | |

A GP2040-CE leverless is ~1–2 ms on both. So the honest claim is **under one
frame behind on press, roughly two frames behind on release** — and both are
*projections* until measured on hardware.

**Why the release debounce stays.** An earlier draft called project 21's 30 ms
release debounce latency conservatism and proposed cutting it to a single poll.
That was wrong, and the repo already contained the evidence: P21's
`ProjectConfig.h` records that the GT911 on this panel "briefly drops/re-reports
a contact (flicker) during a real touch", and sizes 30 ms to "bridge a few
dropped 8 ms frames". Committing a lift on the first empty poll would convert
every flicker into a phantom release — a dropped block or a lost charge. A late
release is survivable; a phantom one is not.

So the two paths are deliberately asymmetric: **presses commit on the first poll
that sees contact, releases are confirmed over `STICK_LIFT_CONFIRM_MS` (24 ms)**.
The 24 is slightly tighter than P21's 30 only because we sample ~4x more often.
It may be lowered only with hardware evidence that flicker is absent.

Note that the flicker risk is currently *masked*: `sampleTouch()` early-returns
while keeping `cachedPointCount` intact, so at today's 8 ms throttle a 2 ms poll
merely re-reads the same non-empty sample. The exposure begins the moment
`CROW_TOUCH_SAMPLE_MS` drops to 2 and every poll becomes a real I2C read.

Hard limits that cannot be designed away:

- **5 simultaneous contacts.** GT911 ceiling. Enough for any normal fighting
  game input; not enough if a palm is also resting on the glass (see Risks).
- **No PS5 or Xbox, ever.** Both require passthrough authentication. PC and
  Nintendo Switch accept generic HID gamepads; that is the entire reachable set.
- **No tactile edge.** Audio clicks and press flashes are the only feedback.
  This is a real, permanent disadvantage and is stated as such in the README.

## Why the existing HID stack cannot be reused as-is

Project 21's `HidBackend` is architecturally a *tap* keyboard. A fightstick is
all *holds*. Three blockers, all verified in source:

1. `CrowHidBackend.cpp:161` — `tapKey()` schedules a release at
   `kHoldMs = 24 ms` (`CrowHidBackend.h:73`) regardless of how long the finger
   is down. You cannot hold back to block.
2. `CrowUsbTransport.cpp:41` — `keyUp()` is `gKeyboard.releaseAll()`. There is
   no per-key release, so no rollover.
3. `CrowHidBackend.cpp:157` — `tapKey()` flushes the previously held key before
   sending a new one, so down + forward + punch serializes into three taps.

Only modifiers have true hold semantics, via `heldMods_`, and game buttons are
not modifiers.

## What makes it tractable

`USBHIDGamepad` ships in core 3.3.8 at
`libraries/USB/src/USBHIDGamepad.{h,cpp}` — a standard TinyUSB gamepad: 32
buttons, an 8-way hat, six axes. No custom report descriptor is needed.

The critical detail is *which* of its methods to use. `pressButton()`,
`releaseButton()`, and `hat()` each call `write()`, emitting one USB report per
call — three buttons means three reports and three frames of skew.
`send(x, y, z, rz, rx, ry, hat, buttons)` sets all state and calls `write()`
**once**.

**The design rule: never call `pressButton`/`releaseButton`. Build the complete
state every poll and emit exactly one `send()` when it differs from the last.**
That makes simultaneous inputs atomic and minimises report count.

## Architecture

Two cores, deliberately split. The DSI panel is single-framebuffer and one
redraw costs tens of milliseconds; if drawing can stall the input loop, the
latency budget is fiction.

```
Core 1 (high priority, no heap, no draw)     Core 0 (Arduino loop)
┌───────────────────────────────────┐        ┌──────────────────────────┐
│ StickTouch.tick()      ~2 ms poll │        │ press-flash rendering    │
│   ↓ up to 5 contacts              │        │ StickEditor (drag/bind)  │
│ hit-test → held bitmask           │ ─────▶ │ StickProfiles (SD)       │
│   ↓                               │  ring  │ StickAudio (click)       │
│ SocdCleaner → hat (0-8)           │  buf   │ SerialCommandRouter      │
│   ↓                               │        └──────────────────────────┘
│ HidBackend::gamepadState(hat,btn) │
│   → send() only when changed      │
└───────────────────────────────────┘
```

Communication is a single-producer / single-consumer ring of pressed-key
bitmasks. The render side never writes it; the stick task never reads back.

### SOCD is structurally enforced

The TinyUSB gamepad hat is a single 0–8 enum, so the wire format is physically
incapable of expressing left+right or up+down. SOCD cleaning is not a rule
layered on top — an illegal input is unrepresentable. The cleaner only decides
*which* legal value results.

Policies: `Neutral`, `LastInput`, `FirstInput`, `UpPriority`. Default is
up-priority for U+D and neutral for L+R, matching common tournament
configuration.

`socdResolve()` is a pure function with no Arduino dependencies, so it host-tests
in g++ alongside the existing LiteGo and Cypher Tune fixtures.

## Components

### Shared library

| File | Responsibility |
|---|---|
| `shared/CrowPanelShared/CrowGamepadTransport.{h,cpp}` | wraps `USBHIDGamepad`; `begin()`, `sendState(uint8_t hat, uint32_t buttons)`; mock path logs to Serial |
| `shared/CrowPanelShared/CrowHidBackend.{h,cpp}` | **new** `gamepadState(hat, buttons)` — change-detected pass-through. No timers, no auto-release, does not touch `tapKey()` or `kHoldMs` |
| `shared/CrowPanelShared/DisplayBringup.h` | **extend** `TouchPointData` with `uint8_t size` (additive; existing callers unaffected) |
| `shared/CrowPanelShared/DisplayBringup.cpp` | read the GT911 per-point size bytes already present in the report |

### Project

| File | Responsibility |
|---|---|
| `src/StickTouch.{h,cpp}` | fork of `KeysTouch`: polls every loop (no 16 ms gate), lift confirmed after 1 empty poll instead of a 30 ms timer, 5-contact ceiling retained |
| `src/SocdCleaner.{h,cpp}` | pure `socdResolve(up,down,left,right,policy,mem) -> hat` |
| `src/StickLayout.{h,cpp}` | `StickKey` / `StickProfile` models and hit-testing |
| `src/StickProfiles.{h,cpp}` | fixed-size binary profile records on SD |
| `src/StickEngine.{h,cpp}` | the core-1 hot loop |
| `src/StickEditor.{h,cpp}` | drag / resize / rebind UI; input suspended while editing |
| `src/StickAudio.{h,cpp}` | per-button click, reusing project 21's `KeyAudio` approach |
| `src/StickThemes.{h,cpp}` | colours and chrome |

### Data model

```cpp
struct StickKey {
  char     label[8];
  int16_t  x, y, w, h;   // rect on the 1024x600 panel
  uint8_t  shape;        // round | rect
  uint16_t color;
  uint8_t  bind;         // gamepad button index, or kBindUp/Down/Left/Right
  uint8_t  key;          // keycode used in keyboard output mode
};

struct StickProfile {
  char     name[16];
  StickKey keys[kMaxKeys];   // kMaxKeys = 20
  uint8_t  keyCount;
  uint8_t  socdPolicy;
};
```

Directions bind to `kBindUp/Down/Left/Right` rather than gamepad buttons so they
feed the SOCD cleaner and become the hat. Everything else maps to a button
index.

## Feature flags

New flag `USE_USB_GAMEPAD`, following the three-layer rule:

1. `AppConfig.h` defines it to `0` under `#ifndef`.
2. `config/ProjectConfig.h` may override for the sketch and `src/`.
3. Because `CrowGamepadTransport.cpp` lives in the shared library and never sees
   `ProjectConfig.h`, real builds **must** pass `-DUSE_USB_GAMEPAD=1` via
   `EXTRA_FLAGS`.

Requires a `USBMode=default` (USB-OTG) FQBN, exactly as project 21 does.

A row must be added to `scripts/check-flag-matrix.sh`; the combination is not
"supported" until that row is green.

Other flags: `USE_DISPLAY`, `USE_USB_HID` (keyboard output mode),
`USE_STICK_SD` (profiles), `USE_STICK_AUDIO` (clicks).

## Modes

- **PLAY** — input live, rendering limited to press flashes.
- **EDIT** — input suspended; drag, resize, and rebind keys.
- **SETTINGS** — SOCD policy, output mode, audio, brightness, profile
  management.

Output toggle mirrors project 21's `OUT` control: `PAD` (`USBHIDGamepad`) or
`KEY` (keyboard). Keyboard mode exists both for PC titles that prefer it and as
a working fallback if the gamepad descriptor misbehaves on a given host.

**Correction (2026-08-01, found while writing the implementation plan):** an
earlier draft of this section called keyboard mode "nearly free since the code
already exists". That was wrong. Every existing keyboard path is *tap*-only —
`UsbTransport::keyUp()` is `gKeyboard.releaseAll()` and there is no per-key
release anywhere in the stack, so holding one key while pressing another is
exactly what it cannot do. Keyboard mode needs `supportsHeldKeys()` /
`keyPressHeld()` / `keyReleaseHeld()` added to `HidTransport` as virtual methods
with default no-op bodies (non-breaking for BLE and projects 05/21), plus a
diffing `HidBackend::keyboardHeldState()`. See Task 15 of the plan.

Keyboard mode also bypasses SOCD cleaning by design: the host sees raw keys, as
it would from a physical keyboard, and only the gamepad hat carries the
structural guarantee.

BLE is **not** offered. It adds latency to a project whose entire premise is
latency, and project 21 already established that BLE mouse reports panic this
NimBLE/esp_hosted stack.

## Deliberate style exceptions

The repo's rule is that transient formatting uses Arduino `String`. **The stick
task suspends that**: no `String`, no heap allocation, no SD access, and no
drawing on the core-1 hot path. This is documented in `TECHNICAL.md` rather than
left for a reader to infer, and is consistent with the existing "long-lived
storage uses fixed buffers" rule.

## Safety boundary

This is a game controller for games the operator is playing. Consistent with the
rest of the repo's boundaries, it deliberately excludes the abusive end:

- **No turbo, no autofire, no input macros.** These are banned in tournament
  play. One press produces one input.
- No recording or replaying of input sequences.
- No network path of any kind.

This boundary belongs in `README.md`, `TECHNICAL.md`, and the safety section of
`docs/full-port-proof-matrix.md`.

## Testing

**Host-side** — `scripts/test-cypher-stick.sh` (g++, following
`test-litego.sh` / `test-cypher-tune.sh`):

- SOCD fixture table: all 16 direction combinations × 4 policies, including
  transitions that exercise the last-input/first-input memory.
- Layout hit-testing: overlapping rects, exact edges, contacts landing in no
  key, round vs rect shapes.
- Profile serialise/deserialise round-trip, including a truncated and a
  corrupt record.

**Flag matrix** — new rows for `USE_USB_GAMEPAD` in `check-flag-matrix.sh`.
Linkage verified by checking `<build-path>/libraries/`, never by a green compile
alone.

**On-panel** — a `bench` serial command reporting the distribution of our own
touch-to-send latency. It cannot measure the GT911's internal half; the report
must say so rather than implying an end-to-end number.

## Risks

1. **Palm rejection is the most likely thing to make this unplayable.** A hand
   resting on the glass consumes one of only five GT911 slots. Contacts outside
   every key rect must be ignored *and* not counted toward the ceiling.
   `TouchPointData` currently exposes only `{x, y, id}` — no contact area — so
   filtering a palm by size requires plumbing the GT911's per-point size bytes
   through `DisplayBringup` first. **Test this before building anything else.**
2. **The dual-core split is the riskiest change.** A race here presents as a
   stuck input, which in a fighting game is indistinguishable from a dropped
   block. The ring buffer must be single-producer/single-consumer with no shared
   mutable state beyond it.
3. **Every latency figure here is a projection.** Nothing may be described as
   achieved until `bench` has run on hardware.
4. Touching `HidBackend` and `DisplayBringup` risks regressing projects 05, 18,
   21, and 22. The `TouchPointData` change is additive, and `gamepadState()` is
   new API rather than a modification of existing calls, but the full flag
   matrix must be green before this is considered done.

## Proof state

Ships as **`compile-ready`**. Not `uploaded`, not `field-proven`. The
`docs/full-port-proof-matrix.md` row, the `README.md` flag table, and the
"NOT HARDWARE-VERIFIED" source comments move together, and only behind evidence
in the session log.

## Out of scope

- PS5 / Xbox support — impossible without passthrough authentication.
- BLE output.
- Physical overlay accessories and layout export to SVG/DXF (considered and
  declined; audio and visual feedback only).
- Training-mode telemetry, input display, and execution grading. An on-screen
  input display is a rendering feature on a latency-critical loop; if it is
  added later it belongs behind its own flag and off by default during play.
