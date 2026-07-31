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

**Uploaded and audible at 32 kHz (2026-07-31); not yet field-proven.**

The engine moved from 22050 Hz to **32 kHz** with different DMA geometry, a
resampled backing-loop voice and table-based pitch math. That is a materially
different binary from the one heard on 2026-07-23, so the proof state was
deliberately reset rather than carried forward — the earlier result stands on
its own terms as field-proven *at 22050*, and this build starts from zero.

Confirmed on hardware so far: the panel's status strip reads `i2s 32k u0`, and
the builtin kit plays with correct pitch and tone — so the rate migration and
the pad resampler are real, not just green in a compile.

Still unverified, and stated plainly because the docs are the point:
- the underrun counter under **sustained load**. `DMA_DESC` went 4 → 6 to hold
  the margin at the higher rate, but that number came from arithmetic, not
  from a measurement.
- **backing-loop playback.** The loop path is the riskiest change in this work
  — a WAV header field the loader never read, a new resampler, and Q32.32 lock
  math — and it has not been exercised on hardware at all.

All flag combinations compile green (119/119 in the flag matrix), and
`./scripts/test-cypher-tune.sh` covers the pitch table and the loop lock
arithmetic on the host (479 checks).

This project also surfaced the amp-enable polarity fix: IO30 is active-LOW,
and driving it HIGH mutes the speaker while I2S keeps streaming.

The audio design targets ~28 ms worst-case pad-to-speaker (see
[TECHNICAL.md](TECHNICAL.md) for the math), and the render task exposes an
underrun counter via `engine` so the timing claim can be checked rather than
trusted. Pad-to-speaker latency has never been captured on video.

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
- **Settings screen** — tap SET for brightness, master volume, theme, kit, and
  idle-dim, plus engine/memory readouts. Keeps the performance screen to just
  what you touch while playing
- **Brightness control** — backlight 40-255, saved across reboots, with
  optional idle dimming that only kicks in while the transport is stopped
- **Themes** — 6 palettes (Ops Teal, MPC Classic, TR-808, Synthwave, Matrix,
  Cotton Candy), on the settings screen or `theme next` / `theme 808` over
  serial; the choice is saved and survives a reboot
- **Animated boot splash** — the wordmark fades up, a drum-shaped waveform
  sweeps across with a glowing leading edge, and the pad grid wipes in
- **Live output scope + VU** — a real oscilloscope of the mix (not a
  simulation) with a peak-hold meter, so you can see the sound you're making
- **Serial parity** — every control also works headless over serial, so the
  whole instrument can be proven without touching the panel

## Technical reference

For installation, build flags, configuration, upload commands, the audio
engine design, kit folder layout, troubleshooting, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
