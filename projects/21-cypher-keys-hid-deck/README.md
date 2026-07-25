# Cypher Keys HID Deck

A touch keyboard, macro pad, and trackpad for your Mac — running on the Elecrow
CrowPanel Advanced 7-inch display and plugged in over USB.

This is the suite's showcase of the ESP32-P4's native USB-HID abilities. The
panel becomes a real USB device: type on an on-screen keyboard, fire shortcuts
from a switchable macro pad above it (a macOS set and a ChatGPT/Codex "micro
keypad" you can extend), and drive the cursor from a full-screen trackpad. It
reuses the polished touch keyboard from Cypher Desk (Project 18).

> This is Project 21 in the [CrowPanel Arduino suite](../../README.md).

## Status

**Field-proven (V1.2, 2026-07-24)** for the full build — `USBMode=default` with
`USE_USB_HID=1 USE_BLE_HID=1 USE_CYPHER_KEYS_AUDIO=1 USE_CYPHER_KEYS_SD=1`
(~1.2 MB, 38% flash / 25% RAM). On a real CrowPanel driving a Mac:

- **USB HID** — macOS binds `ESP32P4_DEV` (VID `0x303A`) as a composite keyboard
  (usage 1/6) + mouse (1/2) + consumer control (12/1); typing, macro presets, the
  app launcher, and media keys all land in host apps.
- **Bluetooth HID** — pairs as `Cypher Keys` with no passkey and types wirelessly
  through the onboard C6.
- **Mechanical key sounds** — audible out of the onboard speaker, both the
  synthesized profiles and real switch sample packs loaded from SD.
- **Touch** — multi-touch chording, hold-repeat, the settings screen, and the
  trackpad (over USB) all behave on glass.

Two honest limits remain:

- **The trackpad is USB-only.** Notifying the mouse HID report over BLE reliably
  panics this NimBLE/esp_hosted stack (keyboard, consumer, and USB mouse are all
  fine), so BLE mouse output is disabled and the trackpad view says so in BLE
  mode. See the [technical reference](TECHNICAL.md) and risk-register row 20.
- Individual edge cases were not each exercised: the ragged-pack fallback
  (`mxblue` has no Backspace clip) and the per-row sample pitch mapping are
  host-tested but were not separately confirmed by ear.

## What you get

- An on-screen QWERTY that types into the host, with real Mac modifiers
  (⌘ ⌥ ⌃), arrows, Tab, and Esc. Modifiers chord for real — hold ⌘ with one
  finger and tap C with another — or tap ⌘ on its own to arm it one-shot
  (tap ⌘ then C → ⌘C). Backspace and the arrows auto-repeat while held
- A macro pad with switchable **presets** shown as tabs; the last one you used is
  remembered across reboots
- A **Mac** preset (Copy/Cut/Paste/Undo, App Switch, Spotlight, Mission Control,
  Screenshot, volume) and a **ChatGPT / Codex** preset (app shortcuts plus canned
  prompt and slash-command snippets) — all editable in one header
- A full-screen **trackpad**: drag to move, tap to click, dedicated left/right
  click buttons, and a scroll strip
- Media and brightness keys via consumer control
- **Mechanical key sounds** through the onboard speaker — clicky *Blue*, tactile
  *Brown* or near-silent *Red*, each with a click going down and a clack coming
  back up, all synthesized on the fly (no sample files). Pick a switch and set the
  volume right on the panel — the settings screen plays a click as you change it,
  so you hear what you picked — or from Serial with `sound`. Either way it is
  remembered across reboots. One flag away (`USE_CYPHER_KEYS_AUDIO=1`), and not
  yet listened to on real hardware
- **Sound packs of real switch recordings, from SD** — drop folders of WAVs on a
  card and the same settings row cycles them alongside the synthesized profiles,
  with per-row and dedicated Backspace / Enter / Space samples, remembered by name
  across reboots. Convert your own with `scripts/convert-key-sounds.sh`; no audio
  ships in this repo, and the synthesized profiles stay the default so no card is
  ever required (`USE_CYPHER_KEYS_SD=1`)
- A **settings screen** behind the `SET` button: key sound, sound volume, screen
  brightness, theme, and idle dimming, each remembered across reboots. With idle
  dimming on, the panel fades down after a minute untouched and the next tap
  brings it straight back — and that tap only wakes the screen, it never types
- **Dual output** — USB *or* Bluetooth, picked with an on-screen `OUT` toggle
  (persisted across reboots). Over Bluetooth the panel is a wireless
  keyboard/mouse via the onboard C6; pairing is passkey-free (Just Works)
- A complete Serial command set that drives the exact same HID paths, so you can
  smoke-test everything with no host attached

## Mock-first, like the rest of the suite

By default the deck is a **mock**: it logs every intended HID report to Serial
and never creates a USB device, so it builds and demos under the standard
`USBMode=hwcdc` flag matrix. The real HID device is one flag away —
`USE_USB_HID=1` with a `USBMode=default` (USB-OTG) build — and the on-screen
status bar and `status` command always tell you which mode you're in (`MOCK` vs
`LIVE`).

## Make the macro pad yours

The presets live in [`config/Macros.h`](config/Macros.h) as a plain, commented
table. Add a preset, rename a tab, point a tile at a different shortcut, or drop
in a canned text snippet — no other file needs to change. This is where you build
out your own Codex/ChatGPT keypad.

## Responsible use

This is a keyboard and mouse for a machine you own. It types and clicks only what
you tap or script; it stores nothing about the host, opens no network connection,
and has no autorun behavior. Treat it like any other input device — it will type
into whatever window has focus.

## Technical reference

For installation, USB-mode explanation, build flags, macro editing, upload
commands, the Serial smoke script, device details, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
