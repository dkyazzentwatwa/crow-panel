# Acid Glass Technical Reference

## Proof boundary

Current state: **partially field-proven 2026-08-01**. The isolated
display-plus-CPU-scaler build rendered animated procedural ASCII scenes on the
real ESP32-P4 panel at 40 FPS. The next isolated build restored GT911 touch;
the live, presets, and visual-parameter drawers responded while animation held
28–31 FPS. These are direct panel
observations. The corrected full-feature build was also observed leaving its
boot diagnostics, rendering animation, and accepting touch. This proves the
combined binary reaches its UI, but it does not yet prove that PPA rather than
CPU fallback is active, SD is mounted, audio is audible, or the C6 AP accepts a
phone and completes remote control requests.

The exact PPA+touch+SD+audio+hosted-C6+remote configuration compiles. Its binary
uses 1,101,112 bytes of flash and 49,204 bytes of static RAM. The repository-wide
matrix baseline section also passed earlier; the complete updated matrix has
not been rerun, so this is not a claim that every matrix row is green.

## Build commands

All commands use the repository's Arduino CLI toolchain and the required local
ctags workaround.

```bash
# Mock-first baseline
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600' \
  --libraries shared --build-property tools.ctags.cmd.path=/usr/bin/true \
  in-progress/24-acid-glass-visualizer

# Full device build
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS='-DUSE_DISPLAY=1 -DACID_GLASS_TOUCH_ENABLED=1 -DACID_GLASS_USE_PPA=1 -DUSE_ACID_GLASS_SD=1 -DUSE_ACID_GLASS_AUDIO=1 -DUSE_WIFI=1 -DUSE_ACID_GLASS_REMOTE=1 -DACID_GLASS_REMOTE_AUTOSTART=1' \
./scripts/compile-all.sh
```

The supported matrix rows are `baseline`, `display-demo`, `display-ppa`,
`display-touch-ppa`, `display-sd`, `sd-audio`, `display-sd-audio`,
`display-remote`, and `full`.

## Architecture

```text
touch / Serial / demo / HTTP
             |
        ControlEvent
             |
       AcidGlassState ---- Preferences presets
         |         |
         |         +---- WAV deck -> I2S speaker
         |                   |
         |              AudioFeatures
         v                   v
        scene renderer at 256x150 RGB565
                         |
              CPU 4x blit (PPA optional)
                         |
                1024x600 DSI framebuffer
                         |
                 HUD + one manual flush
```

The procedural surface is 76,800 bytes. It requests internal SRAM first and
falls back to PSRAM while reporting the actual placement. The feedback surface
lives in PSRAM. PPA failure is visible and falls back to a CPU nearest-neighbor
4× copy rather than producing a blank screen.

The DSI panel is single-framebuffer. `CrowDisplay::begin(..., true)` disables
per-pixel cache flushes. The renderer completes the background, draws the HUD,
then performs one cache synchronization so an incomplete frame is never
deliberately pushed.

## Audio

Startup order is SD_MMC, display, audio, then C6. This preserves the working
CrowPanel SD/DSI order.

The audio task runs on core 0 with six 256-frame DMA descriptors at 44.1 kHz.
The UI and renderer stay on the Arduino loop/core. The onboard amp polarity and
I2S pins come exclusively from `HardwareProfile`; silence is streamed before
the active-low amplifier enable is asserted.

Accepted WAV format:

- RIFF/WAVE PCM format 1
- 16-bit samples
- mono or stereo
- 8,000 through 48,000 Hz
- odd-size chunks are padded according to RIFF rules

When no WAV is indexed, PLAY selects an internal 118 BPM kick, bass, and hat
pattern. It travels through the same I2S output and analyzer path as SD audio,
which makes the speaker stage testable even with an empty card.

The analyzer consumes the decoded samples before volume scaling and publishes
peak, RMS, onset, and eight Goertzel bands centered near 70 Hz through 8.8 kHz.
Its snapshot is protected by a short critical section. Rendering never owns an
SD `File` or the I2S channel.

## Browser API

The AP is open by explicit product choice. Its SSID is MAC-derived so multiple
panels in one room do not collide.

| Method | Route | Behavior |
|---|---|---|
| `GET` | `/` | self-contained mobile controller |
| `GET` | `/api/health` | service, client count, display/audio and literal proof state |
| `GET` | `/api/state` | scene, palette, parameters, track and performance telemetry |
| `POST` | `/api/control` | bounded `action`, `key`, and integer `value` form fields |
| `POST` | `/api/preset` | `load` or `save` and slot `0..15` |

Handlers are registered once. The page polls at 1 Hz; it does not stream panel
frames or compete with SD audio bandwidth.

## Hardware acceptance

Use the repo checklist and advance one stage at a time:

1. Flash `display-demo`; observe a visible Acid Glass scene and responsive HUD.
2. Run every scene and record `status` FPS/frame time for ten minutes. Minimum
   acceptance is 30 FPS per scene; lightweight scenes target 45 FPS or better.
3. Verify swipe, drag, pinch, two-finger feedback, long-press freeze,
   three-finger randomize, and HUD toggle on glass.
4. Add SD only and confirm the track list without starting I2S.
5. Add audio; confirm playback by ear, visible band response, track wrap, volume,
   and zero underruns after five minutes of the heaviest scene.
6. Add the C6 remote; join from a real phone, load `/api/health`, and exercise
   every control route. An advertised SSID is not association proof.
7. Reboot and verify last state plus saved presets survive.

Only after all stages pass should the whole project move from `in-progress/`.
The display, CPU scaler, procedural engine, and basic touch path are already
field-proven; remaining peripherals retain their individual proof boundaries.
