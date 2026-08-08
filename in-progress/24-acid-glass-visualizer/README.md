# Acid Glass

Acid Glass turns the CrowPanel Advanced 7-inch ESP32-P4 into a touch-playable,
SD-backed generative visual instrument. It draws twelve procedural scenes into
a 256×150 RGB565 surface, scales it exactly 4×, and adds a full-resolution
performance HUD before one manual DSI framebuffer flush. The field-proven
bring-up path uses the CPU scaler; PPA is still an opt-in, unverified stage.

The result is intentionally raw, colorful, and physical: ASCII plasma,
kaleidoscopes, tunnels, reaction fields, strange attractors, moiré, pixel melt,
audio scopes, and fractal blooms that respond to music or a built-in synthetic
beat. No video files are involved.

> This is Project 24 in the [CrowPanel Arduino suite](../../README.md). It stays
> under `in-progress/` until the complete binary is observed on a real panel.

## Status

**Partially field-proven 2026-08-01.** The display-only, CPU-scaled staging
build was observed on the real ESP32-P4 panel with animated procedural ASCII
visuals at 40 FPS. The following stage restored GT911 touch: the live,
presets, and visual-parameter drawers responded while animation held 28–31
FPS. The repaired factory preset bank was then exercised on the panel and
visibly changed the scenes and colors. The corrected full-feature binary also
booted past diagnostics into the animated screen with working touch after
freeze persistence was removed from NVS and the first frame was forced during
setup. This does not yet prove the optional PPA, SD, audio, hosted
C6 Wi-Fi, remote control, or their full-feature combination.

Verified in this implementation session:

- the exact professional full configuration compiles on the ESP32-P4 target
- the final full build is 1,101,112 bytes of flash with 49,204 bytes of static RAM
- 1,371 host checks cover scene wrapping, touch mapping, PCM format boundaries,
  and Q16 resampling steps
- the repository-wide matrix baseline section passes for every registered row;
  the remaining pre-existing feature rows were not completed in this session

Still requiring direct device proof: the optional PPA scaler, full gesture
suite, SD enumeration, audible playback, analyzer response, zero-underrun
stress behavior, and phone association/control through the open C6 access
point.

## The visual set

1. ASCII Plasma
2. Glyph Rain
3. Acid Tunnel
4. Kaleidoscope
5. Metaballs
6. Reaction Diffusion
7. Strange Attractor
8. Moire Field
9. Pixel Melt
10. Star Warp
11. Scope Garden
12. Fractal Bloom

Every scene shares twelve palettes and global speed, zoom, intensity, warp,
feedback, trail, symmetry, hue, sensitivity, and X/Y macro controls. Sixteen
named factory presets work immediately; a long press stores a user version over
that slot with a valid NVS header, while a tap loads it. The last active state
also persists in NVS.

## Touch performance

- swipe left/right: next or previous scene
- swipe up/down: next or previous palette
- drag: scene Macro X / Macro Y
- pinch: zoom
- two-finger vertical position: feedback
- long press: freeze or unfreeze
- three-finger tap: randomize
- tap the upper-right corner: show or hide the HUD

## SD music

Put uncompressed PCM WAV files here:

```text
/acid-glass/music/
```

Supported input is 16-bit PCM, mono or stereo, from 8 to 48 kHz. The playback
engine resamples to 44.1 kHz stereo and publishes peak, RMS, onset, and eight
analysis bands to the renderer. If the directory has no WAV files, PLAY starts
the built-in 118 BPM acid beat so the speaker and analyzer can still be tested.
MP3 is deliberately out of scope for v1.

Convert a source file from the repo root:

```bash
./scripts/convert-acid-glass-audio.sh song.mp3 SONG.WAV
```

## Open phone remote

The full build creates `AcidGlass-XXXX` through the onboard C6 and serves the
controller at `http://192.168.4.1/`. It is intentionally an **open network**.
Anyone nearby can change scenes, music, volume, presets, or display settings.
The panel and browser both keep that warning visible.

The API accepts only bounded control values and track indices. It never accepts
filesystem paths or exposes SD files.

## Serial demo

Use 115200 baud with Newline endings:

```text
help
status
scene list
scene Acid Tunnel
palette Toxic Candy
set feedback 180
set macrox 210
randomize
preset save 1
track list
track play 0
demo on
bench 60
```

See [TECHNICAL.md](TECHNICAL.md) for exact builds, architecture, API, and the
hardware acceptance sequence. See [docs/instagram-series.md](docs/instagram-series.md)
for the ten-part shoot plan.
