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

Uploaded and host-enumerated. Every build is green — the mock build under the
suite's default FQBN, and the real USB-OTG HID build (`USBMode=default`,
`USE_USB_HID=1`). That live build has been flashed to a CrowPanel, and macOS
enumerated it as a composite HID device (`ESP32P4_DEV`, VID `0x303A`) with the
keyboard, mouse, and consumer-control interfaces all bound by the system HID
driver — so the panel's USB-C does carry the P4's USB-OTG lines. The last step to
full `host-proven` is watching typed characters, macro shortcuts, media keys, and
cursor moves actually land in a host app.

**Bluetooth (dual mode):** the wireless path (via the onboard C6) is
compile-ready and was proven on this board by a standalone spike — macOS paired
`Cypher Keys` with no passkey and received keystrokes. The integrated dual-mode
build (USB + BLE with the `OUT` toggle) builds green; its on-device acceptance
(toggle, pair, type/macros/media/cursor over BLE) is the last pending step. See
the [technical reference](TECHNICAL.md).

## What you get

- An on-screen QWERTY that types into the host, with real Mac modifiers
  (⌘ ⌥ ⌃), arrows, Tab, Esc, and one-shot sticky modifiers (tap ⌘ then C → ⌘C)
- A macro pad with switchable **presets** shown as tabs; the last one you used is
  remembered across reboots
- A **Mac** preset (Copy/Cut/Paste/Undo, App Switch, Spotlight, Mission Control,
  Screenshot, volume) and a **ChatGPT / Codex** preset (app shortcuts plus canned
  prompt and slash-command snippets) — all editable in one header
- A full-screen **trackpad**: drag to move, tap to click, dedicated left/right
  click buttons, and a scroll strip
- Media and brightness keys via consumer control
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
