# Pip-Boy 3000 CrowPanel Terminal

An unofficial, private Pip-Boy 3000-style showpiece for the Elecrow CrowPanel
Advanced 7-inch display.

Green phosphor, CRT scanlines, and a staged terminal boot turn the panel into a
retro-futuristic prop with a card launcher, a stat page, a map, an item
inspector, an image gallery, and a holotape radio player. It uses only what the
panel already has — the touch display, the SD card slot, the hosted-C6 Wi-Fi, and
the onboard speaker — so there is no enclosure work, no radio module, and no
sensors to add.

> This is Project 20 in the [CrowPanel Arduino suite](../../README.md).

## Status

Field-proven audio core (2026-07-25): the full display+SD+audio+Wi-Fi build was
flashed to a real panel, which booted and auto-played its SD holotape playlist
audibly through the speaker — proving display bringup, SD media discovery, and
WAV playback. Still needing on-panel acceptance: touch navigation, the speaker
test tone, radio controls from glass, and time/weather when Wi-Fi is
configured. See the [technical reference](TECHNICAL.md).

## What you get

- **HOME** — a large-card launcher with CRT scanlines and phosphor styling
- **STAT** — NTP time when available, a read-only weather card, and clearly
  fictional condition gauges
- **MAP** — an SD map image when present, otherwise a built-in exploration grid
- **ITEMS** — a local four-item inspection catalog with original generated art
- **DATA** — an SD BMP gallery with built-in terminal art as a fallback
- **RADIO** — background music: boots straight into the first SD holotape WAV
  and loops the playlist while you browse other tabs, with a generated speaker
  test when no card or track is present; `radio stop` silences it
- Smooth partial-refresh activity cues that never full-repaint just to animate,
  avoiding the flicker of periodic full redraws

## Content and hardware

SD content is optional: copy the `pipboy/` directory to a FAT32 card. V1 accepts
24-bit uncompressed BMP images and 16-bit, 16 kHz mono PCM WAV audio, and all
demo content shipped in the repo is original.

Wi-Fi is opt-in, only for NTP time and weather, and handled entirely by the C6 —
add a 2.4 GHz network to a local `WiFiSecrets.h` and build with `USE_WIFI=1`.

## Responsible use

The terminal never transmits radio traffic or collects information. Weather is
the only network request, and it is user-triggered from the STAT page or Serial.
Keep your `WiFiSecrets.h` out of Git.

## Trademarks

Pip-Boy, Fallout, Vault-Tec, and related marks belong to their respective
owners. This is an unofficial fan prop, not affiliated with or endorsed by
Bethesda or The Wand Company.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
