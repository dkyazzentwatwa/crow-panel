# Cypher Desk OS

An offline-first creator workstation for the Elecrow CrowPanel Advanced 7-inch
display.

The 7-inch panel becomes a small computer: a home-screen app grid with a shared
touch keyboard, a full distraction-free **Writer**, a daily page, local calendar,
contacts, clock, calculator, and file manager, plus a music player and an MJPEG
video player — all working from an SD card with no network. Optional apps add
hosted Wi-Fi setup, weather, and an honestly hardware-gated recorder.

It grew out of a writing deck, and the original writer is now the Writer app
inside this shell — its `/cypher-puter/desk/notes/` layout and existing
preferences stay compatible.

> This is Project 18 in the [CrowPanel Arduino suite](../../README.md).

## Status

Every flag combination is compile-ready, including all of them at once (1.86 MB,
59% of the app partition). The GT911 touch path was proven on a physical panel
earlier.

**Nothing added in the current round has run on hardware.** The audio engine,
the music player, video playback, the reworked keyboard, the boot splash, idle
dim, word wrap and tap-to-place-cursor are all compile-verified only. The WAV
reader, the AVI demuxer and the wrap engine are additionally host-tested (62
checks, and the demuxer has been run against real ffmpeg output), which is not
the same as having been seen on glass. See the
[technical reference](TECHNICAL.md) for the full proof ladder.

## The apps

- **Writer** — Desk, Notebooks, Scrap Jar, Focus sessions, Ritual prompts,
  Archive with full-text and `#tag` search, five themes, atomic saves, exports,
  and a 12,000-character editor
- **Today** — a daily page or quick scrap, today's events, your last note, and a
  recorder shortcut
- **Calendar** and **Contacts** — local, schema-versioned records saved atomically
  to SD
- **Clock** — stopwatch, timer, and a persisted local alarm
- **Calculator** — decimal arithmetic with guarded divide-by-zero, no network
- **Files** — browse the SD workspace, preview text, create/rename/copy/move,
  confirm deletes, report capacity, and safely eject or remount
- **Settings** — themes, hosted-C6 Wi-Fi scans and setup, five saved profiles,
  reconnect, forget, and offline mode
- **Music** and **Podcasts** — browse the card, see each track's real duration
  and format, play with a draggable scrub bar, prev/next, shuffle and repeat.
  Any PCM WAV from 8 kHz to 48 kHz, mono or stereo — a file that will not play
  says why, in the list, instead of failing when you tap it
- **Video** — MJPEG `.avi` clips through the P4's hardware JPEG decoder, with
  the audio track as the sync clock. Clips without audio (including the ones
  project 02 records) play silently rather than being refused
- **Recorder** and **Weather** — guarded, honestly hardware-gated surfaces

## SD is primary, and saves are careful

The workspace lives under `/cypher-puter/desk/` on the card; RAM is only a small,
clearly labeled demo fallback. Notes save through a temporary-file-and-rename
sequence so an interrupted write never destroys the valid copy, malformed
metadata is rebuilt from the folders without touching note contents, and a
low-space guard stops structured writes before the card fills. The storage
service reports every real state — not present, mounting, read-only, corrupted,
full, removed unexpectedly, and more.

## Sound

One audio engine on the IDF `i2s_std` driver — the path projects 09, 20, 21 and
22 are hardware-verified on — running at a fixed 44.1 kHz stereo output with
every source resampled up to it. A mixer task on core 0 drains a 1.5 s buffer
that the main loop fills from the card, so a slow directory read or a
full-screen redraw cannot interrupt playback. Typing sounds are synthesized at
boot in three profiles.

Convert media with `./scripts/convert-crowpanel-audio` guidance and
`./scripts/convert-crowpanel-video.sh`.

## Honest about what isn't proven

None of it has been heard or seen on a panel yet. The amplifier polarity fix
that used to block this project is settled and proven elsewhere in the repo,
which unblocks the work — it does not prove it here.

The recorder never fabricates a WAV: if the microphone or SD does not
initialize, recording simply stays unavailable rather than pretending.

AI, cloud sync, transcription, Bluetooth, and automatic external sends are
intentionally absent. Weather is the one narrow network feature: a user-initiated,
read-only Open-Meteo request with no API key, cached to SD, honest about
captive-portal and unproven-HTTPS conditions.

## Privacy

Everything is local by default. Wi-Fi credentials are masked, never printed, and
stored in the existing NVS namespace — which is not described as encrypted unless
you enable flash encryption yourself. Keep your `WiFiSecrets.h` and other local
config out of Git.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
