# Cypher Boy

A Game Boy and Game Boy Color player for the Elecrow CrowPanel Advanced 7-inch
display.

Drop `.gb` / `.gbc` files on an SD card, tap one on the panel, and play it with
an on-screen touch gamepad. Battery saves are written back to the card, so an
in-game save survives a power-off — which is the whole point if you intend to
play something long.

> This is Project 22 in the [CrowPanel Arduino suite](../../README.md).

## Status

**Field-proven (2026-07-24).** Pokemon Blue loads from SD and plays on the panel
with the touch gamepad and sound. The boot splash, themes, save-state
thumbnails, idle dimming, play-time library sort and the centred gamepad have
all been confirmed on hardware.

Still unexercised: GBC colour on a real `.gbc` title, the cartridge RTC
(Pokemon Gold/Silver/Crystal day-night), and a battery save surviving a full
power cycle. See the [technical reference](TECHNICAL.md).

## You supply the ROM

This project ships **no game ROMs** and never will — Game Boy titles are
copyrighted. Use your own legally-obtained dumps, or a freely-redistributable
homebrew `.gb` if you want something you can show off publicly.

The emulator core itself is [gnuboy](src/gnuboy/VENDORED.md) taken from
[retro-go](https://github.com/ducalex/retro-go), which is **GPLv2** — so this
project folder is GPLv2 too.

## What you get

- A ROM picker that lists everything in `/roms` on the card
- A proper on-screen gamepad: cross D-pad, round A/B, Start/Select, MENU
- Multi-touch friendly — hold a direction while pressing A, as you'd expect
- **Sound** through the panel speaker (`USE_GB_AUDIO=1`), with volume and mute
- **Pause menu** on MENU — resume, save/load state, fast-forward, sound, quit
- **Save states**, 3 slots per game, written to `/states/<rom>.st<n>`
- **Fast-forward** (3x) for grinding through dialogue
- Battery saves written to `/saves/<rom>.sav`, autosaved a couple of seconds
  after the game writes, and flushed whenever you back out to the menu
- Full serial parity: every touch action has a command equivalent, so you can
  drive and smoke-test the whole thing with no panel attached

## SD card layout

```
/roms/     your .gb and .gbc files
/saves/    battery saves, created automatically
/states/   save-state slots, created automatically
```

Both folders are created on first boot if missing.

## Build

Default build is safe and card-free — it runs the emulator core and reports a
placeholder ROM list, so it boots on a bare board:

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

The real thing needs the display, the card, and sound:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1" ./scripts/compile-all.sh
```

## Serial commands

| Command | Does |
|---|---|
| `rom` | List ROMs found on the card |
| `play <n>` | Launch a ROM by index |
| `screen picker` / `screen play [n]` | Switch screens (same paths the touch UI uses) |
| `button up+a` / `button none` | Inject a gamepad press |
| `save` | Force-write battery SRAM now |
| `state save\|load [slot]` | Save-state slots |
| `ff` | Toggle fast-forward |
| `pause` | Toggle the in-game pause overlay |
| `audio vol <0-255>\|mute\|unmute` | Sound control |
| `touch` | Raw + mapped touch point, tap count, held buttons, current screen |
| `selftest` | Drive the flow headlessly with PASS/FAIL/SKIP |
| `status`, `history` | Uptime/heap/flags, recent events |

## Audio

Build with `-DUSE_GB_AUDIO=1` for sound out of the panel speaker. It fails soft:
if I2S will not start, the game keeps running silently rather than crashing.
`audio` over serial reports status, volume, mute, and the underrun count.

With `USE_GB_AUDIO=0` the emulator's mixer still runs but is handed no audio
callback, so nothing is emitted.

## Game Boy Color

GBC works with no extra flags: gnuboy reads the cartridge header and switches
itself into CGB mode. Just drop a `.gbc` file in `/roms`.
