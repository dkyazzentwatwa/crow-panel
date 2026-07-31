# Cypher Keys HID Deck Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/21-cypher-keys-hid-deck.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/21-cypher-keys-hid-deck/TECHNICAL.md.
```

---

A native USB-HID deck for the CrowPanel Advanced 7-inch ESP32-P4: an on-screen
keyboard, a switchable macro pad, and a trackpad that appear to a host (a Mac) as
a USB keyboard, consumer-control device, and mouse. The touch keyboard geometry
is forked from Project 18 (Cypher Desk); the display, touch, and drawing paths
are the shared `CrowPanelShared` library.

## USB modes — the important part

The ESP32-P4 exposes native USB two ways, selected by the FQBN `USBMode` menu:

- `USBMode=hwcdc` (`ARDUINO_USB_MODE==1`) — the suite default. Native USB is the
  hardware CDC/JTAG bridge. **No HID is possible here.**
- `USBMode=default` (`ARDUINO_USB_MODE==0`) — USB-OTG (TinyUSB). This is the mode
  that lets the panel enumerate as a composite keyboard + mouse + consumer-control
  device (with USB-CDC Serial alongside, so the command console still works).

`USE_USB_HID` gates the real device:

| `USE_USB_HID` | FQBN | Result |
|---|---|---|
| `0` (default) | any | MOCK: intended reports are logged to Serial, no USB device |
| `1` | `USBMode=default` | LIVE: real TinyUSB keyboard + consumer + mouse |
| `1` | `USBMode=hwcdc` | Falls back to MOCK with a compile-time `#warning` |

The backend never overrides the platform-owned `build.extra_flags.esp32p4` USB
defines; it only reads `ARDUINO_USB_MODE`. `status` and the on-screen status bar
report `MOCK` or `LIVE` at runtime.

## Bluetooth (dual mode)

The panel can also be a **wireless** keyboard/mouse over Bluetooth-LE, using the
onboard ESP32-C6 as the radio (NimBLE host runs on the P4; HCI is tunneled to the
C6 over esp_hosted). Proven on the tested board: macOS pairs `Cypher Keys` with
**no passkey** (Just Works) and receives keystrokes.

- `USE_BLE_HID=1` enables it. BLE does **not** need USB-OTG (it compiles under
  `hwcdc` too), so the **dual-mode deliverable** is
  `USBMode=default` + `USE_USB_HID=1` + `USE_BLE_HID=1` — USB and BLE both built,
  and an on-screen **OUT** toggle (or `out usb|ble`) picks which one is active
  (one at a time; the choice persists in NVS).
- `BleTransport` sets the hosted SDIO pins with `WiFi.setPins(...)` **before**
  `BLEDevice::init` — mandatory, or the hosted link hangs. It advertises a
  combined keyboard(ID 1)+mouse(ID 2)+consumer(ID 3) HID device.
- Serial: `out usb|ble|toggle`, `ble status`, `ble clear` (erase bonds so the
  host can re-pair — uses NimBLE `ble_store_clear()`).
- Requires the C6 esp_hosted slave firmware to include Bluetooth (v2.12.3 on the
  tested board does). The BLE build is ~1.0 MB (fits the 3 MB app partition).

## Feature flags

- `USE_DISPLAY=1` — the touch UI (keyboard, macro pad, trackpad, status bar).
  Without it the project is Serial-only and still drives every HID path.
- `USE_USB_HID=1` — the real USB-OTG HID device (needs `USBMode=default`).
- `USE_BLE_HID=1` — the Bluetooth-LE HID device via the C6 (works with or without
  USB-OTG; see Bluetooth section).
- `USE_CYPHER_KEYS_AUDIO=1` — synthesized mechanical-switch click sounds out of
  the onboard NS4168 I2S amp (see the next section). Needs `USE_DISPLAY=1` to be
  reachable, since the clicks are driven by the touch keyboard. Tuning knobs:
  `CYPHER_KEYS_AUDIO_SAMPLE_RATE` (22050) and `CYPHER_KEYS_AUDIO_VOLUME` (70).
- `USE_CYPHER_KEYS_SD=1` — **real recorded switch samples** loaded off an SD card
  and played instead of the synthesized profiles (see [SD sound
  packs](#sd-sound-packs-use_cypher_keys_sd)). Needs `USE_CYPHER_KEYS_AUDIO=1` to
  be audible. Knobs: `CYPHER_KEYS_SOUNDS_DIR` (`/cypher-keys/sounds`) and
  `CYPHER_KEYS_SDMMC_1BIT` (`1`, the suite's conservative bring-up).

Panel/backlight knobs (all in `config/ProjectConfig.h`, all display-only):

| Define | Default | Meaning |
|---|---|---|
| `CYPHER_KEYS_BRIGHTNESS` | `255` | boot backlight level, before anything is stored in NVS |
| `CYPHER_KEYS_IDLE_DIM_MS` | `60000` | no-touch time before the backlight ramps down |
| `CYPHER_KEYS_IDLE_DIM_LEVEL` | `24` | how dark idle dimming goes (never above the set brightness) |

## Mechanical key sounds (`USE_CYPHER_KEYS_AUDIO`)

`src/KeyAudio.{h,cpp}` makes typing on the panel *sound* like a mechanical
keyboard: a click on the way down and a clack on the way up, out of the board's
NS4168 I2S amp and speaker.

By default **nothing is sampled and nothing comes off the SD card.** All 18 clips
(3 profiles x press/release x 3 variants) are synthesized at boot with pure float
math into small PCM buffers
(~16 KB total, `ps_malloc` with a `malloc` fallback, kept for the life of the
sketch), the same approach as Project 09's `SynthKit`. These profiles are the
shipped default and stay available in every build — real recorded samples are an
optional upgrade off a card, never a dependency (see [SD sound
packs](#sd-sound-packs-use_cypher_keys_sd)).

### Switch profiles

Each profile has a *press* clip and a *release* clip, and **three variants** of
each (deterministic pitch / level / decay jitter plus its own noise seed), so a
fast typist never hears the same waveform twice running — without that, repeated
taps machine-gun one sample and the illusion collapses.

Every sound is three exponentially decaying layers summed: a one-pole–lowpassed
noise transient (the plastic/metal snap; the lowpass coefficient is the
brightness knob), a resonant *tone* at the switch's click pitch, and a low *body*
thud for bottoming out.

| Profile | Feel | How it is built | Measured centroid / press RMS |
|---|---|---|---|
| `Blue` | clicky | bright noise burst (τ≈1.1 ms) + fast ~2.6 kHz ping (τ≈3.5 ms) + light 190 Hz body; loudest, and a **clearly audible second click** on release (~1.9 kHz at 70% of the press level) | ~2.9 kHz / 3500 |
| `Brown` | tactile | ~1.2 kHz tone, heavily damped (τ≈6 ms), much less and much duller noise, stronger 150 Hz body; quiet release | ~1.6 kHz / 2500 |
| `Red` | linear | almost all 110 Hz thud (τ≈16 ms), a whisper of dull noise, faint 520 Hz; very quiet, release barely there | ~0.8 kHz / 2400 |
| `Off` | silent | nothing is played and the amp is held in its **sleep** state | — |

(Centroid and RMS above come from running the exact `renderClick()` math on the
host: Blue is measurably the brightest and loudest, Red the darkest and
quietest, and no clip clips before the output clamp.)

### Engine

- **I2S bring-up is transcribed from Project 09's `AudioEngine::begin`**, which is
  hardware-proven audible on this board — IDF `<driver/i2s_std.h>` (never the
  Arduino `ESP_I2S` wrapper), `i2s_new_channel` → `i2s_channel_init_std_mode`
  (Philips slot, 16-bit stereo, no MCLK) → `i2s_channel_enable`, with the
  BCLK/LRCLK/SDATA pins read from `HardwareProfile.audio`.
- **A buffer of silence is written before the amp is enabled**, which is what
  keeps the bring-up free of a startup pop. The amp control level comes from
  `profile.audio.controlActiveHigh` (it is **active-LOW** on this board) and is
  never hardcoded; the pin is also parked in its sleep state *before* the bus
  exists, so a failed bring-up leaves a quiet speaker.
- A FreeRTOS render task (`keyclick`, core 0, priority 10) mixes up to **4**
  concurrent one-shots with rolling voice-steal, clamps the sum to int16, and
  paces itself on the blocking `i2s_channel_write`. The main loop is never
  touched, so HID and display latency are unaffected.
- `press()` / `release()` run on the touch path and are deliberately trivial:
  resolve which clip to play, stamp one byte into an SPSC ring. No float math, no
  mixing, no blocking. `press()` fires at touch-**DOWN**, next to the instant
  press highlight, because that is when a real switch actuates.
- Both take the key's **class** (`Generic` / `Backspace` / `Enter` / `Space`) and
  its **row**, from `HidKeyboard::keySoundClass()` / `keySoundRow()`. The
  synthesized profiles ignore both and keep their variant rotation; a sound pack
  uses them to pick its per-row and per-special-key clips.
- Only keyboard keys make sound — macro tiles, the status-bar buttons and
  trackpad motion are silent.
- DMA geometry is Project 09's: 4 descriptors × 128 frames = ~23 ms of queued
  audio at 22050 Hz. That is the click's floor latency on top of the GT911 poll.
- Overlapping clicks are hard-clamped rather than gain-ducked (transients, so
  limiting is inaudible where a gain dip would pump). Clip peaks sit at ~0.6 FS,
  so two simultaneous clicks at the default volume stay clean.
- Profile and volume persist in the existing NVS namespace `cypherkeys` under
  keys `snd` and `sndvol`.
- Both are also **on-screen** controls now: the `KEY SOUND` and `SOUND VOL` rows
  of the [settings screen](#settings-screen), which play a click as you change
  them so you can hear what you picked. `sound` still does everything from
  Serial.

### Without the flag

`KeyAudio` compiles to no-op stubs with the same signatures, so `HidDeck` carries
no `#ifdef`s and `status` / `sound` honestly report `sound: silent
(USE_CYPHER_KEYS_AUDIO=0)`. The whole engine — synthesis, mixer, render task and
the IDF I2S driver — drops out of the binary; what remains in a silent build is
only the `sound` command surface itself (under 1 KB).

### Proof state

**Compile-ready only. Nothing has been heard on hardware.** The `key-audio` flag
matrix row builds green and the linked binary genuinely contains the engine, and
the amp path it uses is byte-for-byte Project 09's, which *is* proven audible on
this panel — but this project's clicks have not been played through a real
speaker yet. On-panel acceptance (audible, correct profile character, no pop at
boot, no dropouts while typing fast) is pending.

## SD sound packs (`USE_CYPHER_KEYS_SD`)

The synthesized profiles are a good impression of a mechanical keyboard. A **real
recording** of one is better. `src/KeySoundPacks.{h,cpp}` loads packs of recorded
switch samples off an SD card into PSRAM and hands them to the same mixer, so
typing on the panel plays actual switch audio — per keyboard row, with dedicated
Backspace / Enter / Space clips.

This is strictly additive: the flag is `0` by default, the synthesized profiles
remain the shipped default and the always-available fallback, and without the flag
neither the loader nor `SD_MMC` reaches the binary at all (see the sizes below).

### Card layout

Pack folders live under `CYPHER_KEYS_SOUNDS_DIR` (default `/cypher-keys/sounds`)
on a FAT32 card:

```text
/cypher-keys/sounds/<pack>/press/GENERIC_R0.wav … GENERIC_R4.wav
/cypher-keys/sounds/<pack>/press/BACKSPACE.wav  ENTER.wav  SPACE.wav
/cypher-keys/sounds/<pack>/release/GENERIC.wav
/cypher-keys/sounds/<pack>/release/BACKSPACE.wav  ENTER.wav  SPACE.wav
```

- Every clip must be **16-bit PCM, mono, at `CYPHER_KEYS_AUDIO_SAMPLE_RATE`**
  (22050 Hz). Anything else is rejected with a reason rather than resampled — the
  engine plays clips 1:1 and there is no resampler on the keypress path.
- Only `press/GENERIC_R0.wav` is required. Real packs are ragged, and the
  fallback order below covers the gaps.
- Unrecognized filenames are ignored (`release/GENERIC_long.wav`, `.DS_Store`,
  macOS `._` sidecars, notes, cover art).
- The folder name is the pack name shown on the panel and used by `sound pack`.
- One pack is resident at a time — the biggest real pack is ~88 KB, and the
  previous one's buffers are freed on every swap.

### Resolution and fallback order

A press resolves to the first of these the pack actually has:

1. the key's **class clip** — `press/BACKSPACE.wav`, `press/ENTER.wav`,
   `press/SPACE.wav`
2. **this row's** `press/GENERIC_R<row>.wav` (panel rows 0–3 map to `R0`–`R3`; a
   row past the pack's rows clamps to `R4`)
3. `press/GENERIC_R0.wav`
4. the **synthesized** press clip for the current profile

A release resolves to:

1. the class clip — `release/BACKSPACE.wav`, `release/ENTER.wav`,
   `release/SPACE.wav`
2. `release/GENERIC.wav`
3. the **synthesized** release clip

That is exactly what makes kbsim's ragged packs work. `mxblue` ships nothing but
`press/GENERIC_R0..R4` and `release/GENERIC`, so its Backspace press falls to
`GENERIC_R2` (Backspace's own row) and every release falls to `GENERIC`. A pack
with no release clip at all simply has no clack, which is a legitimate sound
design rather than an error.

`resolvePressSlot()` / `resolveReleaseSlot()` are pure functions over a
present-slot bitmask, and the filename→slot map and the RIFF parser are pure too,
so all three are covered by the host tests below — including against the real
converted files.

### Loading, and why it never touches the audio task

Mirroring Project 09's `WavLoader`: `beginSd()` (idempotent `SD_MMC` mount),
`sdReady()`, `listPacks()`, `loadPack()`. **All SD I/O happens in loop context.**
The render task never opens a file, and the loader never touches a voice.

- The card is mounted in `HidDeck::begin()` **before** `CrowDisplay::begin()`.
  Mounting `SD_MMC` after the MIPI-DSI framebuffer is live can leave this panel
  backlit but blank (the device-proven order from Projects 02 and 20), so insert
  the card before power-up for a reliable mount.
- A load reads at most 12 files (~88 KB) into fresh `ps_malloc` buffers
  (`malloc` fallback). It happens on a settings tap, a serial command, or once at
  boot, and never on the typing path. The measured elapsed time is reported, so
  the cost is observable rather than guessed at — the line's shape is
  `pack <name>: <n>/12 clips, <n> KB, <n> ms` (no card has been read yet, so no
  real number is quoted here).
- The header parse reads the first 512 bytes into a **file-scope** buffer, so a
  load puts nothing on the loop task's stack. RIFF chunks are walked properly
  (with odd-size pad bytes), because ffmpeg writes a `LIST/INFO` chunk between
  `fmt ` and `data`.
- The declared `data` size is clamped to what the file actually contains and to
  1.0 s of audio, so a truncated or wrong file cannot over-read or eat PSRAM.

**The swap is what makes this safe against the render task.** `KeyAudio` holds
*two* clip sets:

1. `beginPackStaging()` frees and hands out the **inactive** set. The mixer never
   reads it, so filling it from SD is safe while audio keeps playing.
2. `commitPackStaging()` publishes the staged index and bumps a one-byte swap
   ticket (release fence), then waits for the acknowledgement.
3. The render task sees the ticket at the top of its loop, **kills every voice**,
   drops any pending trigger (`trigTail_ = trigHead_`), flips the active set, and
   copies the ticket into the ack.
4. Only *after* that ack does loop context free the retired set's buffers.

So a buffer is never freed while the mixer might still be reading it: the voices
that referenced it are stopped before the swap completes, not merely allowed to
finish. Clip **slots** are resolved in loop context against the live set's
present-mask, so the render task only ever dereferences a slot it was handed, and
a queued trigger cannot survive a swap into a set that lacks that clip. If the
ack never arrives (120 ms — one DMA block is ~6 ms, so this only happens if the
render task is dead), the new pack is still adopted but the old buffers are
deliberately **leaked** rather than freed under a possibly-live mixer.

### Selecting a pack

The settings screen's `KEY SOUND` row and `sound next` now cycle **one flat
list**: `Off`, `Blue`, `Brown`, `Red`, then every pack folder found on the card,
sorted. Landing on a pack loads it there and then, and the row's right-hand value
shows `sd` (a card pack) rather than `i2s` (the synthesized set).

The choice persists **by name** in NVS (`sndpack` in the existing `cypherkeys`
namespace) — a card can be re-imaged, and a pack's position in the list is not
stable. At the next boot the pack is re-loaded after the display and amp are up;
if it is gone, the deck falls back to the stored synthesized profile and says so
in `status`, e.g.

```text
sound: Blue  vol 70%  amp on  i2s 22050Hz ring 23ms  clicks 0
  [pack alpaca: not found under /cypher-keys/sounds/alpaca; staying on Blue]
```

Picking a synthesized profile deselects the pack but keeps it **resident**, so
switching back is instant. Selecting a pack while the profile is `Off` bumps the
profile to `Blue`, so the fallback for a missing clip is always audible.

### Serial

```text
sound                     report profile/pack, volume, engine state, last load
sound off|blue|brown|red  a synthesized profile (persists)
sound packs               list the pack folders on the card
sound pack <name>         load one (persists by name)
sound <name>              same, when <name> is not a profile name
sound next | prev         cycle profiles then packs
sound vol <0-100>         click volume (persists)
```

### Converting packs

`scripts/convert-key-sounds.sh <src-audio-dir> <out-dir> [rate]` turns a
kbsim-style tree (`<pack>/press/*.mp3`, `<pack>/release/*.mp3`) into exactly the
layout above, at 22050 Hz 16-bit mono, with `ffmpeg`:

```sh
./scripts/convert-key-sounds.sh ~/src/kbsim/public/audio ~/Downloads/cypher-keys-sounds
mkdir -p /Volumes/<CARD>/cypher-keys/sounds
cp -R ~/Downloads/cypher-keys-sounds/* /Volumes/<CARD>/cypher-keys/sounds/
```

Thirteen packs convert to ~800 KB total; the largest single pack is 88 KB.

> **Sound packs are deliberately NOT vendored into this repo.** kbsim's code is
> MIT, but it documents no provenance or licence for the recordings themselves, so
> users convert them onto their own card for personal use rather than the suite
> redistributing audio it cannot licence. The synthesized profiles remain the
> shipped default, and every build works with no card at all.

### Host tests

`./scripts/test-cypher-keys.sh` compiles `src/HidKeyboard.cpp`,
`src/KeysTouch.cpp` and `src/KeySoundPacks.cpp` — the exact shipped translation
units — against the tiny shim in `test/shim/`, and runs `test/host_main.cpp`. On
top of the existing chord / sticky / hold-repeat / touch-debounce checks it now
proves:

- the RIFF walk accepts 16-bit mono at the engine rate (with an ffmpeg-style
  `LIST` chunk, and with an odd-sized chunk's pad byte) and rejects stereo,
  8-bit, 44100 Hz, non-RIFF, no-`data` and runt files with a reason;
- the filename→slot map, including that `release/GENERIC_long.wav`,
  `GENERIC_R5`, dotfiles and junk are ignored, and that names are matched
  case-insensitively with the extension stripped;
- the full fallback order for a complete pack, for the `mxblue` shape, for a
  one-clip pack, and for a specials-only pack;
- `HidKeyboard::keySoundClass()` / `keySoundRow()` end to end — tapping `BACK`
  resolves to `press/BACKSPACE.wav` on a full pack and to `press/GENERIC_R2.wav`
  on `mxblue`, `SPACE`'s release resolves to `release/GENERIC.wav`, and a stale
  cross-layer id degrades to generic;
- and, when a converted tree is present, it walks the **real files**: point
  `CYPHER_KEYS_SOUND_DIR` at it (default `~/Downloads/cypher-keys-sounds`). With
  the 13-pack tree it parses 150 real clips through the shipped parser with 0
  rejections and 1 file ignored (`bluealps/release/GENERIC_long.wav`), confirms
  every pack has `press/GENERIC_R0.wav`, and asserts `mxblue`'s mask is exactly
  `GENERIC_R0..R4` + `release/GENERIC`. Those checks **SKIP** (never fail) when
  the tree is absent, so a fresh checkout still reports `ALL PASS`.

### Proof state

**Field-proven (V1.2, 2026-07-24).** All 13 converted kbsim packs were copied to
`/cypher-keys/sounds/` on a 32 GB FAT32 card, mounted on the panel, and played —
the card enumerates, packs load, and the samples are audible. The `key-audio-sd`
flag matrix row builds green *and* `SD_MMC` + `FS` genuinely appear under
`<build-path>/libraries/` (a green build alone would not prove that — see the
`__has_include` note in `src/KeySoundPacks.cpp`), and the parser plus the fallback
order are proven against the real converted WAVs on the host.

Not separately confirmed by ear: the ragged-pack fallback (`mxblue`) and the
per-row pitch mapping — both host-tested. Copy files with
`ditto --norsrc --noextattr` (or clean `._*` afterwards): macOS AppleDouble
shadow files on FAT32 would otherwise appear as bogus pack names.

## Build

Mock builds under the suite's default FQBN (stay green in the shared matrix):

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

The real USB-OTG HID device (note the `USBMode=default` FQBN override):

```sh
CTAGS_WORKAROUND=1 \
FQBN="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_USB_HID=1" \
./scripts/compile-all.sh
```

The full **dual-mode** device (USB + Bluetooth):

```sh
CTAGS_WORKAROUND=1 \
FQBN="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_USB_HID=1 -DUSE_BLE_HID=1" \
./scripts/compile-all.sh
```

With mechanical key sounds (any FQBN — audio does not care about the USB mode):

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CYPHER_KEYS_AUDIO=1" \
./scripts/compile-all.sh
```

With SD sound packs as well (adds `SD_MMC` + `FS`, ~91 KB):

```sh
CTAGS_WORKAROUND=1 \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CYPHER_KEYS_AUDIO=1 -DUSE_CYPHER_KEYS_SD=1" \
./scripts/compile-all.sh
```

Sizes under the suite's default FQBN, which are also the regression floor for
"the SD code compiles out cleanly":

| Build | Flags | Sketch |
|---|---|---|
| baseline | — | 393,822 |
| display | `USE_DISPLAY=1` | 493,608 |
| key sounds | `+ USE_CYPHER_KEYS_AUDIO=1` | 526,336 |
| + SD packs | `+ USE_CYPHER_KEYS_SD=1` | 619,594 |

The shared flag matrix runs a single FQBN, so it exercises Project 21's
`usb-hid-mock`, `ble-hid-mock`, `key-audio` and `key-audio-sd` rows (all under
`hwcdc`). The real USB device needs `USBMode=default`; the real BLE device, the
audio path and the SD loader work either way.

## Upload

Use the same `FQBN` override so the flashed binary matches:

```sh
arduino-cli board list
CTAGS_WORKAROUND=1 \
FQBN="esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_USB_HID=1" \
./scripts/upload-project.sh projects/21-cypher-keys-hid-deck /dev/cu.usbmodemXXXX
```

If the board does not enter the bootloader, hold BOOT while tapping RESET
(`docs/hardware-bringup-checklist.md`, Stage 0). After a HID upload the panel
re-enumerates as a composite device; the CDC serial port returns once macOS
finishes enumerating.

## Macro presets

Presets live in [`config/Macros.h`](config/Macros.h) — an editable table of
`MacroPreset`s, each with up to `CYPHER_KEYS_MACRO_SLOTS` (12 = 3x4) tiles. Slot
kinds (helpers in `src/HidTypes.h`):

- `MACRO_COMBO("Copy", kModCmd, 'c')` — modifiers + one key (ASCII or a `kKey*`
  constant like `kKeyTab`, `kKeyUpArrow`).
- `MACRO_MEDIA("Vol +", kCcVolumeUp)` — a consumer-control usage.
- `MACRO_TEXT("Fix", "Fix the bug: ")` — types a canned string.
- `MACRO_EMPTY` — a blank tile.

Modifiers: `kModCmd` (Command), `kModShift`, `kModOpt` (Option), `kModCtrl`.
Ships with `Mac`, `ChatGPT / Codex`, and `Media` presets. Add or edit freely; the
tab bar and grid resize to the table. The active preset persists in NVS
(`Preferences` namespace `cypherkeys`, key `preset`).

## On-screen layout

Every coordinate and its matching hit-test live together in
[`src/KeysLayout.h`](src/KeysLayout.h), so a drawn panel and its touch target
can never drift apart.

- Top status bar: `CYPHER KEYS`, the `MOCK`/`USB`/`BLE` pill, active preset, last
  action, and four buttons — `OUT`, `DICTATE`, `SET`, and a `TRACKPAD`/`DECK`
  toggle.
- DECK view: preset tabs + a 3x4 macro grid, with the forked keyboard below.
- TRACKPAD view: a large move surface, a right-edge scroll strip, and left/right
  click buttons (press-and-hold on a button supports drag).
- SETTINGS view: a full screen of its own (see the next section).

The status bar was full at four buttons, and sound, volume, brightness and idle
dimming all wanted a fifth. So the third slot stopped being a one-shot `THEME`
cycle and became `SET`, which opens the settings view; theme is a row in there
now. The x bands are unchanged, so `OUT`, `DICTATE` and the mode toggle stay
exactly where muscle memory expects them. The `theme` serial command is
untouched.

## Settings screen

`SET` (or `settings open`) replaces the whole screen with a `< BACK` header and
five rows. Each row is label / stepper / bar-or-name / right-aligned value at a
fixed pitch, so one setting can be repainted and flushed as a single band
without touching the rest of the screen.

| Row | Control | Range | Persisted |
|---|---|---|---|
| `KEY SOUND` | `<` `>` | `Off` / `Blue` / `Brown` / `Red`, then every [SD sound pack](#sd-sound-packs-use_cypher_keys_sd) on the card; the new switch is auditioned on the spot | NVS `snd` + `sndpack` |
| `SOUND VOL` | `-` `+` | 0–100 in steps of 5, drawn as a bar | NVS `sndvol` |
| `BRIGHTNESS` | `-` `+` | `HidDeck::kMinBrightness` (40) to 255 in steps of 24, drawn as a bar | NVS `bright` |
| `THEME` | `<` `>` | prev/next through `deckTheme()` | NVS `theme` |
| `IDLE DIM` | whole row toggles | on / off | NVS `idledim` |

Brightness is floored at 40 rather than 0 because at 0 the panel still renders
but shows nothing — indistinguishable from a crash, with no visible way back to
the `+` button. The bar spans 40–255, so a full-left bar really is the dimmest
the panel goes. The backlight itself goes out through the shared
`CrowDisplay::setBacklight` LEDC helper; nothing in `shared/` needed changing.

Below the rows is a read-only footer: the output mode, free heap, the exact
`sound` engine line, and a reminder that the keyboard and trackpad live on the
deck screen.

Touch is **dispatched by view**: `HidDeck::tick()` routes to
`KeysLayout::hitTestSettings()` while `mode_ == kModeSettings` and returns, so a
stale key, macro tile or status-bar rect can never fire while settings is up
(and vice versa). Taps land on the primary contact's *release*, exactly like the
deck's chrome buttons, so a press and its release can never straddle a view
change. `BACK` returns to whichever view opened settings — deck or trackpad.

## Idle dimming

With `IDLE DIM` on, `CYPHER_KEYS_IDLE_DIM_MS` (60 s) of no touch ramps the
backlight down to `CYPHER_KEYS_IDLE_DIM_LEVEL` (24, and never above the set
brightness) over about 0.6 s — stepped from the main loop, so HID and Serial keep
running through the fade. A resting finger counts as activity, which is why the
panel can only ever start dimming with nothing down.

**The wake tap is consumed.** The gesture that lights the panel back up is
swallowed whole, from touch-down through release: it restores full brightness
instantly and does *not* type a key, fire a macro, or move the pointer. Every
contact's key binding is dropped for the duration, so a pending release cannot
replay as a key-up and leave a modifier stuck down. Serial `bright`, `idledim`
and `settings` also count as activity and wake the panel.

## Boot splash

[`src/KeysSplash.{h,cpp}`](src/KeysSplash.cpp) — the wordmark fades up out of
the background, then ten keycaps spelling `CYPHERKEYS` land left to right (each
lit as it arrives, settling as the next comes down) drawing an accent rule in
behind them. The subtitle is the HID backend's mode, so the first thing on screen
is the truth about where keystrokes will go.

It runs once from `HidDeck::begin()` after the display is up and before the first
UI paint. Boot is the one moment nothing competes for the framebuffer, so it
draws straight into the cached FB and syncs one region per frame — the same
manual-flush path the deck itself uses, no offscreen canvas needed. Total is
~1.2 s on purpose: this is a keyboard, and anything longer is a wait rather than
an intro. Without a display it compiles to a no-op, so `begin()` carries no
`#ifdef`.

The keyboard is multi-touch (`KeysTouch` tracks up to 5 GT911 contacts), so
`⌘ ⌥ ⌃` and shift work two ways: **hold** one with a finger and tap another key
for a real chord (⌘+C), or **tap** one on its own to arm it one-shot for the next
key, exactly as before. Every press is resolved at touch-DOWN and hit-tested
against the down position, so sliding a finger off a key cannot fire a different
one. Backspace and the arrows auto-repeat while held
(`CYPHER_KEYS_KEY_REPEAT_DELAY_MS` then `CYPHER_KEYS_KEY_REPEAT_MS`).
`123`/`ABC` toggles the symbols layer (which adds `Esc` and `Tab`); a layer flip
drops any held modifier, because key ids are relative to the live layer.

## Serial commands

115200 baud, line ending Newline. Every command runs the same `HidBackend` path
the touch UI uses.

- `status` / `history` — shared
- `hid` — backend mode (MOCK/LIVE) and interface list
- `key <text>` — type a literal string
- `combo <mods+key>` — e.g. `combo cmd+c`, `combo cmd+shift+4`, `combo ctrl+up`
- `tap <n>` — fire macro slot `n` (0-11) in the active preset
- `preset <next|name>` — switch preset (persists)
- `mode <deck|trackpad>` — switch view
- `mouse <dx> <dy>` — move the cursor
- `click <l|r>` — mouse click
- `scroll <steps>` — mouse wheel
- `media <volup|voldn|mute|play|brightup|brightdn>` — consumer-control key
- `dictate` — tap F5 (macOS dictation/mic key)
- `sound` — report the key-click profile or pack, volume and I2S state
- `sound <off|blue|brown|red>` — pick a synthesized switch profile (persists)
- `sound packs` — list the SD sound packs on the card
- `sound pack <name>` / `sound <name>` — load an SD sound pack (persists by name)
- `sound <next|prev>` — cycle profiles, then packs
- `sound vol <0-100>` — click volume (persists)
- `settings` — report brightness, idle dim, theme and the live view
- `settings <open|close>` — show or leave the settings screen
- `bright` / `bright <0-255>` / `bright <+|->` — panel backlight, floored at 40
  (persists)
- `idledim <on|off|toggle>` — idle dimming (persists)
- `touch` — raw and mapped touch diagnostics

`status` grows a `panel:` line carrying the same brightness / idle-dim / theme /
view summary that `settings` prints, so there is only one place that formats it.

> Before 2026-07-31 the shared router's command table was capped at 12 entries
> and silently dropped every later registration, so `dictate`, `theme`, `out`,
> `ble`, `sound`, `settings`, `bright`, `idledim` and `touch` never dispatched
> over Serial — the touch UI was unaffected. See `CROW_SERIAL_MAX_COMMANDS`
> in `AppConfig.h`.

### Serial smoke (mock, no host)

```text
status
hid
key hello world
combo cmd+shift+4
preset next
tap 0
mode trackpad
mouse 25 0
click l
scroll -3
media volup
mode deck
sound
sound brown
sound vol 55
sound next
sound packs
sound pack mxblue
settings open
bright -
bright 255
idledim off
idledim on
settings close
history
```

Each line should print an `[hid] mock: ...` report describing the intended USB
event.

## Proof states

- `compile-ready`: baseline, display, `usb-hid-mock`, `key-audio` and
  `key-audio-sd` build under `hwcdc`, and the real `USBMode=default` +
  `USE_USB_HID=1` build all compile. (All green.) `./scripts/test-cypher-keys.sh`
  reports `ALL PASS`.
- `uploaded`: **done** — the `USBMode=default` HID binary was flashed to a real
  CrowPanel (`Hash of data verified`).
- `host-enumerated`: **done** — macOS bound `ESP32P4_DEV` (VID `0x303A`, PID
  `0x2`) as a composite HID device. Verify with:
  ```sh
  ioreg -r -c IOHIDInterface -l | grep -A2 ESP32P4_DEV   # DeviceUsagePairs
  hidutil list | grep 303a
  ```
  Observed `DeviceUsagePairs`: `{1,6}` keyboard, `{1,2}`/`{1,1}` mouse+pointer,
  `{12,1}` consumer control.
- `host-proven`: **done (V1.2, 2026-07-24)** — on the full build
  (`USBMode=default` + `USE_USB_HID=1 USE_BLE_HID=1 USE_CYPHER_KEYS_AUDIO=1
  USE_CYPHER_KEYS_SD=1`, ~1.2 MB / 38% flash / 25% RAM), typing, macro presets,
  the Spotlight app launcher, media keys, and the trackpad were all observed
  landing in host apps over USB, and the panel pairs as `Cypher Keys` and types
  wirelessly over BLE.
- `key sounds`: **field-proven** — audible out of the onboard NS4168 speaker.
  This is what the amp-enable active-LOW fix (risk register row 18) unblocked.
- `SD sound packs`: **field-proven** — all 13 converted kbsim packs mounted from
  `/cypher-keys/sounds/` and played. The WAV parser and the per-key fallback
  order were additionally proven against the real files on the host
  (`./scripts/test-cypher-keys.sh`, 150 clips parsed, 0 rejected).
- `settings screen / multi-touch / idle dim / boot splash`: **field-proven** —
  multi-touch modifier chording, hold-repeat, the settings rows, and the splash
  all behave on glass.

### Known limit: the trackpad is USB-only

Notifying the **mouse** HID report over BLE reliably panics this
NimBLE/esp_hosted stack (`rst:PANIC`) within seconds of using the trackpad, while
the keyboard (report 1) and consumer control (report 3) over the same connection
are solid and the USB mouse is fine. It is **not** simple buffer exhaustion:
report coalescing (66/s → 45/s → 25/s), a fast preferred connection interval
(7.5–15 ms), and hard flow control gating notifies on `os_msys_num_free()` all
failed to prevent it. The backtrace is unreachable over USB because P4 panic
output goes to UART0.

`BleTransport`'s mouse methods are therefore hard no-ops and the trackpad view
shows **TRACKPAD: USB ONLY** in BLE mode, so the crash path is unreachable. To
revisit: capture the panic backtrace via a USB-UART adapter on UART0 TX, or try
exposing the mouse as a *separate* BLE HID device. Tracked as risk-register
row 21.

### Not separately confirmed by ear

Two audio details are host-tested but were not individually A/B'd on hardware:
the ragged-pack fallback (kbsim's `mxblue` ships no `BACKSPACE`/`ENTER`/`SPACE`
clips, so those keys should fall back to a row generic) and the per-row sample
pitch mapping (panel rows map to sample rows R2–R4 in the letters layer, R1–R4 in
symbols — derived from kbsim's `KeySimulator.js` row switch and its default KLE
preset, whose row 0 is the function row).

## Safety boundary

This is a standard USB input device for a machine you own. It transmits only the
keystrokes, media keys, and pointer moves you tap or script; it reads nothing
from the host, makes no network connection, and runs nothing automatically on
connect. It will type into whatever window currently has focus, so treat it like
any other keyboard.
