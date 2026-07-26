---
name: convert-crowpanel-audio
description: Use when converting music, songs, samples, or sound files (mp3, m4a, flac, ogg, wav) for CrowPanel projects — SD-card audio for the Pip-Boy holotape radio, Cypher Desk, Tune MPC kits, or Cypher Keys sound packs — or when a WAV on the card is rejected ("invalid WAV", "need 16-bit 16k mono WAV", silent playback).
---

# Convert Audio for CrowPanel

## Overview

Every CrowPanel audio engine plays raw **16-bit PCM, mono** WAV at a fixed
per-project sample rate — no on-device resampling on the playback path. The
firmware RIFF parsers are strict, so conversion must produce a clean
`RIFF → fmt → data` layout with no metadata chunks.

## Quick Reference

| Project | Rate | SD destination |
|---|---|---|
| 20 Pip-Boy holotape radio | 16000 | `/pipboy/audio/` (first 16 `.wav` indexed) |
| 18 Cypher Desk | 16000 | see project TECHNICAL.md |
| 09 Cypher Tune MPC kits | 22050 | `/cypher-tune/loops/<kit>/` + `pack.txt` |
| 21 Cypher Keys packs | 22050 | `/cypher-keys/sounds/` — use `./scripts/convert-key-sounds.sh` instead |

Rates are authoritative in each project's `config/ProjectConfig.h`
(`*_AUDIO_SAMPLE_RATE` / `*_ENGINE_RATE`) — check there if in doubt.

## The Command

```bash
ffmpeg -v error -y -i "input.mp3" -ac 1 -ar 16000 -c:a pcm_s16le \
  -map_metadata -1 -fflags +bitexact "output.wav"
```

- `-ac 1 -ar <rate> -c:a pcm_s16le` — the format every engine expects.
- `-map_metadata -1 -fflags +bitexact` — **required**: without these ffmpeg
  inserts a `LIST/INFO` chunk between `fmt ` and `data`, which some of the
  firmware parsers reject.
- Name outputs short, lowercase, numbered: `01-track-name.wav`. Long names
  overflow the on-panel track display. Note the firmware indexes in FAT
  directory order (the order files were copied), not alphabetically — copy the
  files to the card in the intended sequence if play order matters.

Verify one output before copying a batch to the card:

```bash
ffprobe -v error -show_entries stream=codec_name,sample_rate,channels -of default=nw=1 output.wav
xxd -l 48 output.wav   # expect: RIFF...WAVEfmt then data — no LIST
```

## Rules

- **Never commit converted audio to the repo.** Copyrighted music (Fallout
  soundtrack, sound packs, etc.) stays in a local staging folder
  (`~/Downloads/...`) that the user copies to the FAT32 card themselves.
- Stage in the card's directory shape (e.g. `pipboy/audio/`) so the whole
  folder can be dragged onto the SD root — SD paths in the table are relative
  to the card root. The card must be FAT32, not exFAT.
- Batch loudness varies wildly between sources; if tracks are jarringly
  uneven add `-af loudnorm=I=-16:TP=-1.5` (slower, but one consistent level).

## Common Mistakes

- Stereo or 44.1/48 kHz output → firmware rejects it or plays garbage; there
  is no resampler on the SD playback path.
- Skipping `-map_metadata -1` → `LIST` chunk → "invalid WAV" on strict parsers.
- More files than the project indexes (Pip-Boy caps at 16) → silent drops.
- Converting Cypher Keys packs by hand — `scripts/convert-key-sounds.sh`
  already handles that tree layout and naming.
