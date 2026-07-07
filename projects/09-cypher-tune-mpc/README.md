# CrowPanel Cypher Tune MPC

Touch groovebox inspired by `cardputer-mpc`.

V1 includes a 4x4 pad/sample engine, 16-step transport, BPM, play/stop, record,
visual voices, and an opt-in generated-click I2S path behind `USE_AUDIO`.
Default builds stay silent/mock so the project is safe to compile without audio
hardware or sample files.

## Serial Commands

- `help` / `status` / `history`
- `pad <1-16>`
- `step <1-16> [pad]`
- `bpm <value>`
- `play`
- `stop`
- `record`
- `pattern`
- `samples`
- `voices`
- `audio`

## Build Flags

Compile the mock Serial path:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

Compile with the shared dashboard/display path enabled:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
```

Compile the project with the experimental audio path enabled:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_AUDIO=1" ./scripts/compile-all.sh
```

`USE_AUDIO=1` uses the ESP32 Arduino `ESP_I2S` wrapper when available and emits a
short generated click on pad/step triggers. It does not decode WAV/MP3 samples
yet and is not speaker-verified.

Project-local tuning flags:

- `CYPHER_TUNE_AUDIO_SAMPLE_RATE` defaults to `22050`.
- `CYPHER_TUNE_AUDIO_CLICK_FRAMES` defaults to `96`.
- `CYPHER_TUNE_AUDIO_VOLUME` defaults to `96`.

## Samples, Latency, And Proof

No actual samples are required in this repo. The current pad map uses
`factory:*` placeholder refs so Serial and display smoke tests work cleanly.
When real samples land, prefer SD or external flash storage with short 16-bit
PCM/WAV files first; keep large compressed formats out of the critical live-pad
path until decode latency is measured on the CrowPanel.

Current proof language:

- `compile-ready`: baseline, `USE_DISPLAY=1`, and `USE_AUDIO=1` builds compile.
- `uploaded`: sketch was flashed to a real CrowPanel.
- `field-proven`: pad-to-sound latency, amplifier enable, and speaker output
  were verified on the exact board revision and documented with Serial logs or
  video.
