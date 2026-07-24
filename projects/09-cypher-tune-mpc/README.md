# Cypher Tune MPC

A real touch groovebox for the Elecrow CrowPanel Advanced 7-inch display.

A 4x4 velocity-sensitive pad grid you play with your fingers, a 16-step /
16-pad / 4-pattern sequencer with swing and a metronome, per-pad volume,
pitch, and choke groups, and a polyphonic sample engine that mixes eight
voices out the board's NS4168 I2S speaker amp. Sounds come from a built-in
synthesized drum kit (no files needed) or from 16-bit WAV kits on the SD
card, switchable live from the touch screen.

Default builds are still silent by design: the whole transport, pattern
engine, and serial control surface work with no audio hardware and no sample
files present.

> This is Project 9 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready: baseline, `USE_DISPLAY=1`, `USE_AUDIO=1`,
`USE_AUDIO=1 USE_MPC_SD=1`, and the combined display+audio builds all compile
for the real ESP32-P4 target. Nothing has been heard on a speaker yet —
pad-to-sound latency, amplifier enable, and speaker output still need to be
verified on the exact board revision. The audio design targets ~26-29 ms
worst-case pad-to-speaker (see [TECHNICAL.md](TECHNICAL.md) for the math);
the render task reports an underrun counter so the timing claims can be
checked on hardware instead of trusted.

## What you get

- **Touch pad grid** — 4x4, MPC layout (pad 1 bottom-left), fires on
  press-down with velocity from where you hit the pad; two-finger drumming
  works (5-contact multi-touch tracking)
- **Step lane** — TR-style: pressing a pad selects it, its 16 steps show on
  the right; tap a step to cycle off → on → accent
- **4 patterns (A-D)** with per-step-per-pad velocity, switchable from the
  transport bar
- **Swing** (50-75%, MPC convention) and a **metronome** with a downbeat
  accent
- **Pad edit panel** — per-pad volume and pitch sliders (±12 semitones) and
  choke groups (closed hat chokes open hat out of the box)
- **Sound engine** — 8-voice PCM mixer over I2S, sample-accurate sequencing
  driven by the audio clock itself, record mode that quantizes live hits
- **Kits** — a built-in 16-sound synthesized kit rendered at boot, plus SD
  card WAV kits (`/mpc/kits/<name>/pad01.wav … pad16.wav`) hot-swappable
  while the transport runs
- **Serial parity** — every control also works headless over serial, so the
  whole instrument can be proven without touching the panel

## Technical reference

For installation, build flags, configuration, upload commands, the audio
engine design, kit folder layout, troubleshooting, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
