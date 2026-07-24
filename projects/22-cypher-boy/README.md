# Cypher Boy

A Game Boy and Game Boy Color player for the Elecrow CrowPanel Advanced 7-inch
display.

Drop `.gb` / `.gbc` files on an SD card, tap one on the panel, and play it with
an on-screen touch gamepad. Battery saves are written back to the card, so an
in-game save survives a power-off — which is the whole point if you intend to
play something long.

> This is Project 22 in the [CrowPanel Arduino suite](../../README.md).

## Status

**Compile-ready, not yet played on hardware.** All four build combinations
compile green for the real ESP32-P4 target, and the headless `selftest` passes.
Nothing here has been observed running on a physical CrowPanel: a ROM actually
booting, touch driving gameplay, a battery save surviving a reboot, and GBC
colour output are all still open. See the
[technical reference](TECHNICAL.md) for the exact acceptance steps.

## You supply the ROM

This project ships **no game ROMs** and never will — Game Boy titles are
copyrighted. Use your own legally-obtained dumps, or a freely-redistributable
homebrew `.gb` if you want something you can show off publicly.

The emulator core itself is [gnuboy](src/gnuboy/VENDORED.md) taken from
[retro-go](https://github.com/ducalex/retro-go), which is **GPLv2** — so this
project folder is GPLv2 too.

## What you get

- A ROM picker that lists everything in `/roms` on the card
- An on-screen gamepad: D-pad, A, B, Start, Select, and a MENU key
- Multi-touch friendly — hold a direction while pressing A, as you'd expect
- Battery saves written to `/saves/<rom>.sav`, autosaved a couple of seconds
  after the game writes, and flushed whenever you back out to the menu
- Full serial parity: every touch action has a command equivalent, so you can
  drive and smoke-test the whole thing with no panel attached

## SD card layout

```
/roms/     your .gb and .gbc files
/saves/    battery saves, created automatically
```

Both folders are created on first boot if missing.

## Build

Default build is safe and card-free — it runs the emulator core and reports a
placeholder ROM list, so it boots on a bare board:

```bash
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

The real thing needs the display and the card:

```bash
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_GB_SD=1" ./scripts/compile-all.sh
```

## Serial commands

| Command | Does |
|---|---|
| `rom` | List ROMs found on the card |
| `play <n>` | Launch a ROM by index |
| `screen picker` / `screen play [n]` | Switch screens (same paths the touch UI uses) |
| `button up+a` / `button none` | Inject a gamepad press |
| `save` | Force-write battery SRAM now |
| `touch` | Raw + mapped touch point, tap count, held buttons, current screen |
| `selftest` | Drive the flow headlessly with PASS/FAIL/SKIP |
| `status`, `history` | Uptime/heap/flags, recent events |

## Audio

v1 is **silent** by design. The emulator's mixer runs but is given no audio
callback, so nothing is emitted. Sound through the NS4168 I2S amp is the first
planned follow-up once the video, touch, and save chain is proven on glass.
