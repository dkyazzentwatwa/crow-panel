# 20 - Pip-Boy 3000 CrowPanel Terminal Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/20-pipboy-terminal.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/20-pipboy-terminal/TECHNICAL.md.
```

---

An unofficial, private Pip-Boy 3000-style showpiece for the 7-inch CrowPanel
ESP32-P4. It uses the panel's touch display, SD_MMC card slot, hosted-C6 Wi-Fi,
and onboard I2S speaker path. No external controls, enclosure work, radio
hardware, microphone, or sensors are required.

## Experiences

- **HOME** is a modern, large-card launcher with CRT scanlines and green
  phosphor styling.
- **STAT** shows NTP time when available, a read-only Open-Meteo weather card,
  and clearly fictional condition gauges.
- **MAP** loads `/pipboy/images/wasteland-map.bmp` when present, otherwise it
  uses a built-in exploration grid and markers.
- **ITEMS** is a local four-item inspection catalog with generated original art.
- **DATA** is an SD BMP gallery with built-in terminal art as a fallback.
- **RADIO** lists SD holotape WAV files, plays them over I2S, and exposes a
  generated speaker test when there is no card or track.
- **Boot and motion** use a 3.2-second staged terminal start plus small,
  partial-refresh activity cues on every page. The panel never full-repaints
  merely to animate, avoiding the display flicker seen with periodic redraws.

The terminal never transmits radio traffic or collects information. Weather is
the only network request and is user-triggered from STAT or Serial.

## SD card

See [`assets/README.md`](assets/README.md). Copy its `pipboy/` directory to a
FAT32 card root. V1 accepts 24-bit uncompressed BMP images and 16-bit, 16 kHz,
mono PCM WAV files. All source-controlled demo content is original.

## Wi-Fi

Wi-Fi is opt-in. Copy `config/WiFiSecrets.example.h` to the gitignored local
`config/WiFiSecrets.h`, add a 2.4 GHz network, and build with `USE_WIFI=1`.
The C6 handles the radio link; the P4 only renders the terminal and requests
time/weather through the shared hosted-Wi-Fi path.

`PIPBOY_TZ` in `config/ProjectConfig.h` defaults to Pacific time. Change that
POSIX timezone string locally before filming in another timezone.

## Serial controls

| Command | Action |
| --- | --- |
| `status` | display page, SD, media, network, and proof state |
| `page home\|stat\|map\|items\|data\|radio` | open a page |
| `radio play\|next\|stop\|test\|volume N` | control local media |
| `weather` | request a weather refresh |
| `storage` | print SD media status |
| `touch` | show last mapped touch point |

## Build

```sh
# Display only
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" \
./scripts/compile-all.sh

# SD media only. Prove card mount and gallery/map loading before enabling Wi-Fi.
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_PIPBOY_SD=1" \
./scripts/upload-project.sh projects/20-pipboy-terminal /dev/cu.usbmodemXXXX

# Hosted-C6 Wi-Fi only. Requires local WiFiSecrets.h; prove NTP and weather.
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_WIFI=1" \
./scripts/upload-project.sh projects/20-pipboy-terminal /dev/cu.usbmodemXXXX

# Combined storage + Wi-Fi. Add USE_PIPBOY_AUDIO later, after the speaker path
# has its own acceptance test.
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_PIPBOY_SD=1 -DUSE_WIFI=1" \
./scripts/upload-project.sh projects/20-pipboy-terminal /dev/cu.usbmodemXXXX
```

## Proof state

- `compile-ready`: baseline, display, SD, audio, Wi-Fi, and full rows compile.
- `uploaded`: not claimed until the selected full build writes to a real panel.
- `device-proven`: requires visible touch navigation, SD media discovery, an
  audible speaker test and WAV playback, plus time/weather evidence when Wi-Fi
  credentials are configured.

Pip-Boy, Fallout, Vault-Tec, and related marks are owned by their respective
owners. This is an unofficial fan prop, not affiliated with or endorsed by
Bethesda or The Wand Company.
