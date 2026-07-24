# Cypher Desk OS

An offline-first creator workstation for the Elecrow CrowPanel Advanced 7-inch
display.

The 7-inch panel becomes a small computer: a home-screen app grid with a shared
touch keyboard, a full distraction-free **Writer**, a daily page, and local
calendar, contacts, clock, calculator, and file manager — all working from an SD
card with no network. Optional apps add hosted Wi-Fi setup, weather, and honestly
hardware-gated media and recording.

It grew out of a writing deck, and the original writer is now the Writer app
inside this shell — its `/cypher-puter/desk/notes/` layout and existing
preferences stay compatible.

> This is Project 18 in the [CrowPanel Arduino suite](../../README.md).

## Status

The baseline and the display-plus-SD-plus-Wi-Fi builds are compile-ready. The
GT911 touch path was proven on a physical panel earlier, but the OS launcher,
tap-on-release flow, local data persistence, SD recovery, hosted-C6 Wi-Fi states,
audio, and microphone each need their own new device acceptance before they can
be called proven. See the [technical reference](TECHNICAL.md) for the full proof
ladder.

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
- **Recorder, Music, Podcasts, Weather** — guarded, honestly hardware-gated
  surfaces (see below)

## SD is primary, and saves are careful

The workspace lives under `/cypher-puter/desk/` on the card; RAM is only a small,
clearly labeled demo fallback. Notes save through a temporary-file-and-rename
sequence so an interrupted write never destroys the valid copy, malformed
metadata is rebuilt from the folders without touching note contents, and a
low-space guard stops structured writes before the card fills. The storage
service reports every real state — not present, mounting, read-only, corrupted,
full, removed unexpectedly, and more.

## Honest about what isn't proven

Media and recording are compiled but deliberately not claimed. Audio (key sounds
and ambient WAV loops) stays compile-only until the speaker and amplifier pass a
bench test. The recorder never fabricates a WAV — if the microphone or SD does
not initialize, recording simply stays unavailable rather than pretending.

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
