# Cypher Desk OS Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/18-cypher-desk-panel.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/18-cypher-desk-panel/TECHNICAL.md.
```

---

Project 18 is an offline-first creator command desk for the CrowPanel. The
original writing deck is now the complete **Writer** app inside a reusable
touch application shell. Its `/cypher-puter/desk/notes/` contract and the
existing `cypher-desk` Preferences namespace remain compatible.

## OS application catalog

The home screen is a 3-column by 4-row grid with a persistent system status
line. OS-native screens use tap-on-release and the shared touch keyboard.

- **Writer** preserves Desk, Notebooks, Scrap, Focus, Ritual, Archive, themes,
  atomic saves, exports, statistics, and the 12,000-character editor.
- **Today** opens a Daily Page or quick scrap, shows today’s events, the last
  note, local storage state, and a Recorder shortcut.
- **Calendar** and **Contacts** keep local schema-versioned records and save
  them atomically to SD.
- **Clock** includes stopwatch, timer, and an NVS-persisted local alarm toggle.
- **Calculator** supports decimal arithmetic, sign, percent, and guarded
  divide-by-zero handling without a network.
- **Files** browses the SD workspace, previews supported text, creates folders,
  renames, copies, moves, confirms deletes, reports capacity, and safely ejects
  or remounts the card. Internal metadata, cache, and backup paths are protected.
- **Settings** provides themes, asynchronous hosted-C6 scans, visible or hidden
  network entry, five saved profiles, reconnect, forget, and offline mode.
- **Recorder** writes named 16 kHz mono PCM WAV files directly to SD only when
  the guarded microphone path initializes. It exposes a level meter, stop/save,
  and last-recording playback without simulating unavailable hardware.
- **Music** and **Podcasts** browse SD folders and attempt only validated local
  16 kHz mono PCM WAV playback. Podcasts make no RSS or download claim.
- **Weather** uses the same Open-Meteo forecast model as the ADS-B scanner,
  behind the Desk Wi-Fi service. It needs a user-supplied coordinate pair and
  verified internet, caches the last successful result on SD, and stays honest
  about unavailable network or unproven HTTPS conditions.

## Writer app

The persistent bottom dock opens **Desk, Notebooks, + Scrap, Focus, and
Ritual**. Archive and Settings live in the header.

- **Desk** continues the last document, opens today's Daily Page, shows three
  recent notes, reports storage honestly, and summarizes quiet writing time.
- **Notebooks** treats one-level folders as notebooks. Notes remain ordinary
  `.md` and `.txt` files and can be favorited, finished, exported, moved,
  renamed, or deleted with confirmation.
- **Scrap Jar** opens a blank Markdown scrap immediately. The first non-empty
  line becomes its title. Save, discard, and move-to-notebook are available.
- **Focus** offers explicit 10, 20, 30, and 45-minute sessions. The typewriter
  view centers five lines around the cursor and retains the touch keyboard,
  word count, save state, timer, pause, resume, early finish, and +5 minutes.
- **Ritual** includes forty built-in prompts across Morning Pages, Observation,
  Memory, Scene, and Letter. Each category shuffles without repeats until it
  cycles. `/cypher-puter/desk/prompts.txt` can replace the built-in set when it
  contains exactly forty non-comment lines.
- **Archive** filters the existing library by Recent, Favorites, Finished,
  Daily Pages, and Scraps, with shared-keyboard full-text search and `#tag`
  filtering. Finished notes never move from their original path.
- Writer settings retain the five themes, offline date, sound preferences, and
  safe-eject flow. System Wi-Fi setup is also available from the OS Settings
  app.

Small labels and keyboard actions use the U8g2 `cubic11` font through
Arduino_GFX.

## Storage model

SD is primary. RAM is a clearly labeled, small, nonpersistent demo fallback.

```text
/cypher-puter/desk/
├── notes/
│   ├── daily/
│   ├── scraps/
│   └── user notebooks/
├── audio/          ambience loops (rainy-cafe.wav, vinyl-room.wav, ...)
├── recordings/
├── music/          any PCM WAV, 8-48 kHz, mono or stereo, 8- or 16-bit
├── podcasts/       same formats as music/
├── video/          MJPEG .avi clips (see scripts/convert-crowpanel-video.sh)
├── documents/
├── calendar/
│   └── events.tsv
├── contacts/
│   └── contacts.tsv
├── exports/
├── backups/
├── cache/
└── .desk/
    ├── index.tsv
    └── sessions.csv
```

Daily Pages use `notes/daily/YYYY-MM-DD.md`. Notes save through a temporary
file and rename sequence so an interrupted write does not first destroy the
valid copy. Missing or malformed metadata is rebuilt from the note folders
without changing note contents. Archive queries are paged beyond the original
64-note browser limit. Export creates a timestamped copy in `exports/`.

Completed and early-ended sessions append to `sessions.csv` with date,
duration, starting words, ending words, document path, and completion state.
Only explicitly started sessions are recorded. The UI intentionally avoids
streaks, rankings, goals, and warning language.

The shared storage service reports `NotPresent`, `Mounting`, `Mounted`,
`ReadOnly`, `UnsupportedFilesystem`, `Corrupted`, `Full`,
`RemovedUnexpectedly`, and `Error`. A low-space guard stops new structured
writes below the larger of 5 percent free or 32 MiB. Boot recovery restores a
known backup when the primary structured file is absent and removes stale
temporary files only when the primary is intact.

## Wi-Fi, time, and privacy

`USE_WIFI=1` enables a nonblocking hosted-C6 state machine for scans, setup,
reconnect, connectivity status, and NTP. A compile-configurable HTTP 204 probe
distinguishes verified internet, connected-without-internet, and a suspected
captive portal. The device warns about portals but does not embed a browser.

Up to five recent profiles are stored in the existing NVS namespace.
Credentials are masked and never printed. Ordinary NVS is not described as
encrypted unless flash encryption is independently enabled. Pacific time is
the default and `CYPHER_DESK_TIMEZONE` remains compile-configurable.

AI, cloud sync, transcription, Bluetooth, podcast downloading, and automatic
external sends are intentionally not included. Weather is the narrow exception:
it is a user-initiated, read-only Open-Meteo request with no API key.

## Optional audio and guarded recording

`USE_CYPHER_DESK_AUDIO=1` enables the compile-gated `DeskAudio` service. It can
generate Pencil, Typewriter, and Mechanical key sounds and stream 16-bit,
16 kHz mono PCM WAV ambience from `audio/`:

- `rainy-cafe.wav`
- `vinyl-room.wav`
- `fireplace.wav`
- `brown-noise.wav`

One generated key sound is mixed over ambience with clipping protection.
Ambience defaults to Off and key sounds to low-volume Pencil. Missing files,
unsupported WAV headers, or I2S initialization failure return silently to no
sound. The feature must remain compile-ready only until the speaker pins and
amplifier path pass a dedicated bench test.

`USE_CYPHER_DESK_RECORDER=1` enables the guarded PDM recording adapter. It
never creates synthetic WAV data: failure to initialize the microphone or SD
keeps recording unavailable. Promotion from compile-ready requires verified
input pins and format, level metering, a short recording, reboot persistence,
and real playback.

## Build matrix

Run the Project 18 rows in `scripts/check-flag-matrix.sh`, or use these flags:

```text
baseline                 (no flags)
display                  -DUSE_DISPLAY=1
display + SD             -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1
display + SD + time      -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_WIFI=1
display + SD + audio     -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_AUDIO=1
recorder guard           -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_RECORDER=1
display + SD + media     -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_MEDIA=1 -DUSE_CYPHER_DESK_AUDIO=1
display + SD + video     -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_CYPHER_DESK_AUDIO=1 -DUSE_CYPHER_DESK_MEDIA=1 -DUSE_CYPHER_DESK_VIDEO=1
full OS                  -DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1 -DUSE_WIFI=1 -DUSE_CYPHER_DESK_AUDIO=1 -DUSE_CYPHER_DESK_MEDIA=1 -DUSE_CYPHER_DESK_VIDEO=1 -DUSE_CYPHER_DESK_RECORDER=1
```

Everything together is 1.86 MB, 59% of the 3 MB app partition.

Apps whose feature is compiled out are neither built nor registered, so a
build without `USE_CYPHER_DESK_MEDIA` or `USE_CYPHER_DESK_VIDEO` carries none
of their UI and the launcher shows no tile for them.

## Host tests

The WAV reader, the AVI demuxer and the editor's word-wrap engine are free of
SD_MMC and display headers, so the exact translation units that ship also
build with a plain `g++`:

```sh
./scripts/test-cypher-desk.sh              # 62 checks
./scripts/test-cypher-desk.sh clip.avi     # inspect a real file with the same parser
./scripts/test-cypher-desk.sh track.wav
```

The file mode is the quickest way to find out whether a clip or track will
play before copying it to the card.

## Media

Audio runs through one `DeskAudioEngine` on the IDF `i2s_std` driver - the
path projects 09, 20, 21 and 22 are hardware-verified on - at a fixed
44.1 kHz stereo output, with every source resampled up to it. That is what
lets 44.1 kHz stereo music, a 16 kHz ambience loop and a key click share the
output without reconfiguring the clock mid-playback, and it makes the played-
frame counter usable as a video sync clock.

A FreeRTOS mixer task on core 0 drains a 1.5 s PSRAM ring that loop context
fills from the card. **The mixer task never opens a file** - break that and
audio deadlocks behind a slow card.

Video is MJPEG-in-AVI through the P4's hardware JPEG decoder and PPA scaler,
both already linked by core 3.3.8. Audio is the master clock: frame N is
presented when the played-frame counter passes N x microSecPerFrame, and a
frame more than one frame late is dropped rather than queued. Clips with no
audio stream - including project 02's own `VID_*.AVI` recordings - play
silently off the wall clock.

Prepare clips with:

```sh
./scripts/convert-crowpanel-video.sh input.mp4
```

512 px wide at 15 fps is about 286 KB/s, comfortable on SD_MMC in 1-bit mode.
The player reports dropped frames on screen; if that number climbs, lower the
width or frame rate.

Example upload:

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CYPHER_DESK_SD=1" \
./scripts/upload-project.sh projects/18-cypher-desk-panel <PANEL_PORT>
```

## Serial QA

> **These were all broken until 2026-07-31.** The shared `SerialCommandRouter`
> held 12 commands and `on()` silently refused the rest; this sketch registers
> 35, so everything from `page` onward in registration order — `scrap`, `focus`,
> `ritual`, `theme`, `sound`, `stats`, `search`, `find`, `time`, `storage`,
> `app`, `apps`, `wifi`, `calc`, `calendar`, `contacts`, `alarm`, `media`,
> `audio`, `recovery`, `weather`, `video`, 23 in total — never dispatched. The
> touch UI was unaffected. See `CROW_SERIAL_MAX_COMMANDS` in `AppConfig.h`;
> the table now holds 128 and a non-zero drop count is reported by `status`,
> `help` and the boot log.

Existing commands remain: `status`, `history`, `files`, `new`, `open`, `type`,
`save`, `back`, `demo`, and `touch`.

Writer QA commands are also available:

```text
page desk|notebooks|scrap|focus|ritual|archive|settings
daily
scrap
focus 10|20|30|45
ritual shuffle|write
theme [name]
sound key 0..3
sound ambience 0..4
sound volume 0..100
stats
search <text>
search tag <tag>
time sync|timezone|zone <POSIX TZ>|prev|next|confirm
storage rebuild|eject
```

OS and service QA commands:

```text
app home|writer|today|calendar|contacts|clock|calculator|files|settings|recorder|music|podcasts|weather
apps
wifi scan|offline|online|saved N|forget N
weather status|refresh|location <latitude> <longitude> [label]
os-events
calc 7|+|=|C
calendar [add YYYY-MM-DD HH:MM title|edit N YYYY-MM-DD HH:MM title|note N text|alarm N on|off|delete N]
contacts [add name|set N name|org|phone|email|notes value|delete N]
alarm on|off
alarm timer <minutes>
media
audio speaker|mic|record [name]|play <path>|stop|volume N|status
recovery run
```

## Proof states

- **compile-ready** means the matching feature row builds for ESP32-P4.
- **uploaded** means the matching binary was flashed successfully.
- **SD-proven** requires create, reboot, reopen, metadata rebuild, export, and
  macOS/Cardputer readability on a real card.
- **time-proven** requires a real hosted-C6 connection and observed NTP update.
- **audio-proven** requires the dedicated speaker and mixed-WAV bench pass.
- **microphone-proven** requires level, record, reboot, and playback evidence.
- **complete field-proven** requires the 3x4 launcher, every routed app, shared
  keyboard, Writer autosave, local structured data, storage recovery, Wi-Fi
  state transitions, date, and graceful-failure checks on the physical panel.

The cached GT911 touch path was previously proven on the physical panel. The OS
launcher, tap-on-release flow, structured data, SD recovery, hosted-C6 state
machine, audio path, and microphone path need their own new device acceptance.
