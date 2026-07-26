# Handoff: portrait-mode touch mapping is wrong (Cypher Vision Cam)

## The one-line problem

In portrait orientation the UI **renders correctly** but **touch lands in the
wrong place**. Landscape is fine. Two candidate inverse transforms have been
tried and both are wrong, so the obvious "it's 180° out" explanation is already
eliminated.

## Hardware and build

- Board: Elecrow CrowPanel Advanced 7", **ESP32-P4**, V1.2, 1024x600 MIPI-DSI,
  GT911 capacitive touch.
- Project: `projects/02-cypher-vision-cam/`
- Arduino core `esp32:esp32@3.3.8`. Rendering is **Arduino_GFX**, not LVGL.

Build and flash (a local ctags bug makes `CTAGS_WORKAROUND=1` mandatory):

```bash
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CAMERA_DRIVER=1 -DUSE_CAM_SD=1 -DUSE_WIFI=1" \
./scripts/upload-project.sh projects/02-cypher-vision-cam /dev/cu.usbmodem1101
```

Put the board in download mode first: **hold BOOT, tap RESET, release BOOT**,
then check `arduino-cli board list` for the port (it moves between `usbmodem101`,
`usbmodem1101`, `usbmodem3101`).

**Regression gate before you finish** — several rows compile these files without
display or SD, so stubs matter:

```bash
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

## THE CRITICAL CONSTRAINT: there is no serial console

With `USBMode=hwcdc` the USB serial port **disappears the moment the app starts
running**. You cannot printf-debug this. Every diagnostic must be **drawn on the
panel**. This is the single biggest thing to internalise before starting; it has
caught everyone working on this board.

## What is already known to be true

### Rendering rotation works

`VisionCamUi::setOrientation` calls `Arduino_GFX::setRotation(1)` (or `3` for the
flipped variant) and the entire UI draws correctly rotated. **Do not go looking
for a rendering bug — there isn't one.** The layout is a runtime `Layout` struct
(`computeLayout(portrait)`), logical canvas 600x1024 in portrait.

### The driver's actual rotation transform

From `Arduino_DSI_Display::writePixelPreclipped`
(`~/Documents/Arduino/libraries/GFX_Library_for_Arduino/src/display/Arduino_DSI_Display.cpp:60`):

```cpp
case 1:
  fb += (int32_t)x * _fb_width;   // framebuffer ROW    = logical x
  fb += _fb_max_x - y;            // framebuffer COLUMN = 1023 - logical y
```

with `_fb_width = 1024`, `_fb_max_x = 1023` (line 19-22).

So for **rotation 1**, forward transform is:

```
physical_col = 1023 - logical_y
physical_row = logical_x
```

and the inverse (what touch needs) is:

```
logical_x = physical_row      // i.e. panel y
logical_y = 1023 - physical_col
```

### What touch actually delivers

`VisionCamUi::handleTouch_` reads `touch_.releaseX()` / `releaseY()` from
`CrowTouch` (`shared/CrowPanelShared/TouchInput.cpp`). Those are **already
mapped**: `CrowTouch::mapX/mapY` scale the raw GT911 values through
`CROW_TOUCH_MIN_X/MAX_X/MIN_Y/MAX_Y` and clamp to **x in [0,1023], y in
[0,599]** — i.e. physical panel coordinates, landscape.

Defaults in `shared/CrowPanelShared/AppConfig.h` are identity
(`MIN=0, MAX_X=1023, MAX_Y=599, SWAP_XY=0, INVERT_X=0, INVERT_Y=0`), and
landscape touch is accurate, so that layer is believed sound.

### What has been tried and failed

`VisionCamUi::mapTouch_` currently implements both inverses:

```cpp
if (portraitFlipped_) {        // rotation 3
  x = kPanelH - 1 - rawY;      // 599 - panel_y
  y = rawX;
} else {                       // rotation 1
  x = rawY;
  y = kPanelW - 1 - rawX;      // 1023 - panel_x
}
```

Settings → Orientation cycles LANDSCAPE → PORTRAIT → PORTRAIT FLIPPED.
**The user reports both portrait states are "badly off".**

That both fail is the most useful clue available: a simple 180° ambiguity would
have made exactly one of them correct. Something other than the rotation
direction is wrong.

## Hypotheses worth testing, roughly in order

1. **Axis range mismatch.** `CrowTouch` clamps y to 0..599 and x to 0..1023.
   After the portrait swap the code expects logical x in 0..599 and logical y in
   0..1023 — check the actual numbers rather than the intended ones. A clamp
   applied in the wrong space would compress one axis and could look like a
   wildly wrong hit location.
2. **`CrowTouch` is doing its own transform you are not accounting for.** Read
   `mapX`/`mapY` carefully; note they take *both* raw axes as arguments
   (`mapX(rawX, rawY)`) because of the `SWAP_XY` option.
3. **Hit rectangles vs draw calls disagree.** Both are supposed to read the same
   `Layout` fields. Verify the portrait bar in particular: it has THREE rows
   (`btnY` shutter, `actionY` REC/PAUSE, `navY` navigation) and the hit tests
   must use the same fields.
4. **The GT911 itself may report differently than assumed.** Nothing has ever
   verified the raw values against physical positions on this board.

## Strongly recommended approach: measure, do not derive

Two derivations have now failed. **Add an on-screen calibration mode** and read
the numbers off the panel:

- Draw a crosshair at the *mapped* touch point, and print both the raw
  (`touch_.rawX/rawY`), the CrowTouch-mapped (`touch_.x/y`), and the
  portrait-mapped values as large text.
- Touch each of the four corners and the centre in portrait, and write down what
  appears. That gives the transform directly, with no reasoning required.
- There is already a `touch` serial command and an `orient mark` crosshair
  helper, but **remember serial is dead** — surface it on the panel instead.

Once the true mapping is known, encode it in `mapTouch_` and delete the
`portraitFlipped_` fallback if it turns out to be unnecessary.

## Files that matter

| File | Role |
|---|---|
| `projects/02-cypher-vision-cam/src/VisionCamUi.cpp` | `mapTouch_` (the bug), `computeLayout`, `handleTouch_`, `setOrientation` |
| `projects/02-cypher-vision-cam/src/VisionCamUi.h` | `CamOrientation`, layout/state members |
| `shared/CrowPanelShared/TouchInput.cpp` | `CrowTouch::mapX/mapY`, release-edge debounce |
| `shared/CrowPanelShared/AppConfig.h` | `CROW_TOUCH_*` calibration macros |
| `~/Documents/Arduino/libraries/GFX_Library_for_Arduino/src/display/Arduino_DSI_Display.cpp` | ground-truth rotation transform, line 55-95 |

## Repo conventions to respect

- `AGENTS.md` and `CLAUDE.md` at the repo root are binding. Read them first.
- **The honesty contract matters here.** Source comments distinguish
  `compile-verified` from `hardware-verified`; `docs/full-port-proof-matrix.md`
  and `docs/hardware-risk-register.md` track proof state. Do not upgrade a claim
  past the evidence. If you fix this, say it is hardware-verified only once it
  has been seen working on the panel.
- Every sketch must **define functions before use** — the ctags workaround skips
  prototype generation, so forward references fail to compile.
- 2-space indent, `PascalCase` types, `camelCase` members with a trailing
  underscore for privates.

## Definition of done

1. In portrait, tapping a control activates **that** control, at all four
   corners and the centre.
2. Landscape is unchanged.
3. `check-flag-matrix.sh` green (all 7 P02 rows) and `compile-all.sh` green
   (all 20 sketches).
4. Verified **on the panel**, not merely compiled — and the comment in
   `mapTouch_` updated to record the transform that was actually measured, with
   the reasoning, so the next person does not re-derive it.
