# Cypher Keys HID Deck Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/21-cypher-keys-hid-deck.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/21-cypher-keys-hid-deck/TECHNICAL.md.
```

---

A native USB-HID deck for the CrowPanel Advanced 7-inch ESP32-P4: an on-screen
keyboard, a switchable macro pad, and a trackpad that appear to a host (a Mac) as
a USB keyboard, consumer-control device, and mouse. The touch keyboard geometry
is forked from Project 18 (Cypher Desk); the display, touch, and drawing paths
are the shared `CrowPanelShared` library.

## USB modes — the important part

The ESP32-P4 exposes native USB two ways, selected by the FQBN `USBMode` menu:

- `USBMode=hwcdc` (`ARDUINO_USB_MODE==1`) — the suite default. Native USB is the
  hardware CDC/JTAG bridge. **No HID is possible here.**
- `USBMode=default` (`ARDUINO_USB_MODE==0`) — USB-OTG (TinyUSB). This is the mode
  that lets the panel enumerate as a composite keyboard + mouse + consumer-control
  device (with USB-CDC Serial alongside, so the command console still works).

`USE_USB_HID` gates the real device:

| `USE_USB_HID` | FQBN | Result |
|---|---|---|
| `0` (default) | any | MOCK: intended reports are logged to Serial, no USB device |
| `1` | `USBMode=default` | LIVE: real TinyUSB keyboard + consumer + mouse |
| `1` | `USBMode=hwcdc` | Falls back to MOCK with a compile-time `#warning` |

The backend never overrides the platform-owned `build.extra_flags.esp32p4` USB
defines; it only reads `ARDUINO_USB_MODE`. `status` and the on-screen status bar
report `MOCK` or `LIVE` at runtime.

## Feature flags

- `USE_DISPLAY=1` — the touch UI (keyboard, macro pad, trackpad, status bar).
  Without it the project is Serial-only and still drives every HID path.
- `USE_USB_HID=1` — the real USB-OTG HID device (needs `USBMode=default`).

## Build

Mock builds under the suite's default FQBN (stay green in the shared matrix):

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

The real USB-OTG HID device (note the `USBMode=default` FQBN override):

```sh
CTAGS_WORKAROUND=1 \
FQBN="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_USB_HID=1" \
./scripts/compile-all.sh
```

The shared flag matrix runs a single FQBN, so it exercises Project 21's
`usb-hid-mock` row (the flag under `hwcdc`, which compiles to the mock). The real
device is the separate `USBMode=default` build above.

## Upload

Use the same `FQBN` override so the flashed binary matches:

```sh
arduino-cli board list
CTAGS_WORKAROUND=1 \
FQBN="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_USB_HID=1" \
./scripts/upload-project.sh projects/21-cypher-keys-hid-deck /dev/cu.usbmodemXXXX
```

If the board does not enter the bootloader, hold BOOT while tapping RESET
(`docs/hardware-bringup-checklist.md`, Stage 0). After a HID upload the panel
re-enumerates as a composite device; the CDC serial port returns once macOS
finishes enumerating.

## Macro presets

Presets live in [`config/Macros.h`](config/Macros.h) — an editable table of
`MacroPreset`s, each with up to `CYPHER_KEYS_MACRO_SLOTS` (12 = 3x4) tiles. Slot
kinds (helpers in `src/HidTypes.h`):

- `MACRO_COMBO("Copy", kModCmd, 'c')` — modifiers + one key (ASCII or a `kKey*`
  constant like `kKeyTab`, `kKeyUpArrow`).
- `MACRO_MEDIA("Vol +", kCcVolumeUp)` — a consumer-control usage.
- `MACRO_TEXT("Fix", "Fix the bug: ")` — types a canned string.
- `MACRO_EMPTY` — a blank tile.

Modifiers: `kModCmd` (Command), `kModShift`, `kModOpt` (Option), `kModCtrl`.
Ships with `Mac`, `ChatGPT / Codex`, and `Media` presets. Add or edit freely; the
tab bar and grid resize to the table. The active preset persists in NVS
(`Preferences` namespace `cypherkeys`, key `preset`).

## On-screen layout

- Top status bar: `CYPHER KEYS`, the `MOCK`/`LIVE` pill, active preset, last
  action, and a `TRACKPAD`/`DECK` toggle.
- DECK view: preset tabs + a 3x4 macro grid, with the forked keyboard below.
- TRACKPAD view: a large move surface, a right-edge scroll strip, and left/right
  click buttons (press-and-hold on a button supports drag).

The keyboard's `⌘ ⌥ ⌃` keys are one-shot sticky modifiers; shift is one-shot too.
`123`/`ABC` toggles the symbols layer (which adds `Esc` and `Tab`).

## Serial commands

115200 baud, line ending Newline. Every command runs the same `HidBackend` path
the touch UI uses.

- `status` / `history` — shared
- `hid` — backend mode (MOCK/LIVE) and interface list
- `key <text>` — type a literal string
- `combo <mods+key>` — e.g. `combo cmd+c`, `combo cmd+shift+4`, `combo ctrl+up`
- `tap <n>` — fire macro slot `n` (0-11) in the active preset
- `preset <next|name>` — switch preset (persists)
- `mode <deck|trackpad>` — switch view
- `mouse <dx> <dy>` — move the cursor
- `click <l|r>` — mouse click
- `scroll <steps>` — mouse wheel
- `media <volup|voldn|mute|play|brightup|brightdn>` — consumer-control key
- `touch` — raw and mapped touch diagnostics

### Serial smoke (mock, no host)

```text
status
hid
key hello world
combo cmd+shift+4
preset next
tap 0
mode trackpad
mouse 25 0
click l
scroll -3
media volup
mode deck
history
```

Each line should print an `[hid] mock: ...` report describing the intended USB
event.

## Proof states

- `compile-ready`: baseline, display, and `usb-hid-mock` build under `hwcdc`, and
  the real `USBMode=default` + `USE_USB_HID=1` build all compile. (All green.)
- `uploaded`: **done** — the `USBMode=default` HID binary was flashed to a real
  CrowPanel (`Hash of data verified`).
- `host-enumerated`: **done** — macOS bound `ESP32P4_DEV` (VID `0x303A`, PID
  `0x2`) as a composite HID device. Verify with:
  ```sh
  ioreg -r -c IOHIDInterface -l | grep -A2 ESP32P4_DEV   # DeviceUsagePairs
  hidutil list | grep 303a
  ```
  Observed `DeviceUsagePairs`: `{1,6}` keyboard, `{1,2}`/`{1,1}` mouse+pointer,
  `{12,1}` consumer control.
- `host-proven`: the remaining step — with focus in a host app (TextEdit, etc.),
  tap the on-screen keys, fire macro presets, press media keys, and use the
  trackpad, and confirm each lands. Serial shows `LIVE` in `status`.

The USB-OTG HID path is now hardware-observed on this panel; only the end-to-end
input-landing check is left for `host-proven`.

## Safety boundary

This is a standard USB input device for a machine you own. It transmits only the
keystrokes, media keys, and pointer moves you tap or script; it reads nothing
from the host, makes no network connection, and runs nothing automatically on
connect. It will type into whatever window currently has focus, so treat it like
any other keyboard.
