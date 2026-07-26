# CrowPanel Cypher Tune MPC Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/09-cypher-tune-mpc.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/09-cypher-tune-mpc/TECHNICAL.md.
```

---

Touch groovebox inspired by `cardputer-mpc`, grown into a real instrument:
polyphonic sample engine, sample-accurate sequencing, full touch UI. Default
builds stay silent/mock so the project is safe to compile without audio
hardware or sample files.

## Architecture

| Module | Role |
|---|---|
| `src/Sequencer` | 4 patterns x 16 steps x 16 pads (velocity bytes), swing, transport state, record quantize; frame-clock API for the engine + `tickMillis()` fallback for silent builds |
| `src/SampleBank` | Owns 16 `PadSound`s (PSRAM PCM + gain/pitch/choke/label); two banks so SD kits stage while the other plays |
| `src/SynthKit` | Boot-time synthesis of the 16-sound builtin kit + metronome clicks (pure float math into PCM buffers) |
| `src/AudioEngine` | IDF `i2s_std` output, FreeRTOS render task on core 0, 8-voice mixer (16.16 resampler = rate conversion + pitch, Q12 gains, choke/steal fades), SPSC command+event rings; the render task is the musical clock |
| `src/WavLoader` | SD kit scan + RIFF parse (16-bit PCM mono, 8-48 kHz kept at native rate), builtin fallback per missing pad file |
| `src/TuneUi` | Full-screen MPC UI: dirty-region rendering with `CrowDisplay` manual flush, state-diff mirrors so serial edits appear on screen |
| `src/TouchTracker` | 5-contact GT911 tracking (id + proximity matching, press/release edges, release debounce) over the shared `CrowDisplay::touchPoints()` |
| `src/UiLayout.h` | Every pixel coordinate + hit-test in one header so drawing and touch cannot disagree |
| `src/TuneSplash` | Boot animation: wordmark fade-up, damped-sine waveform sweep with a hot leading edge, staggered pad-grid wipe-in. Draws into the cached FB with per-frame region flushes (boot is the one moment nothing competes for the panel), ~2.3 s, theme-aware, no-op without a display |
| `src/TuneThemes` | 6 groovebox palettes (`TuneTheme`: chrome + pad + step + playhead roles). Same idea as project 21's `DeckTheme`, kept project-local and tuned punchier for pads. Selection persists in NVS (`CYPHER_TUNE_NVS_NAMESPACE`); available headless so `theme` works without a display |

### Audio path and latency

- The Arduino `ESP_I2S` wrapper is **not** used: core 3.3.8 hardcodes
  `dma_desc_num=6, dma_frame_num=240` (~65 ms of queued audio at 22.05 kHz).
  The engine drives IDF `driver/i2s_std.h` directly with
  `CYPHER_TUNE_DMA_DESC` (4) descriptors of `CYPHER_TUNE_BLOCK_FRAMES` (128)
  frames: a 23 ms ring, ~26-29 ms worst-case pad-to-speaker. Unverified on
  hardware; the `engine` command's underrun counter is the empirical check.
- NS4168 I2S amp (LRCLK 21, BCLK 22, SDATA 23, amp enable IO30 active-high):
  a digital-input Class-D amp, no codec registers, no I2C init. The engine
  streams two silent blocks before raising IO30 to avoid the wake pop.
- The render task (core 0, priority 10) is paced by the blocking
  `i2s_channel_write` and counts output frames: sequencer steps fire at exact
  frame boundaries (blocks are split at step edges), swing offsets odd 16ths,
  record quantizes against the frame clock. `loop()`/UI stay on core 1.
- Threading: loop context is the only command producer and event consumer.
  Multi-field transport actions (`play`/`stop`/trigger/kit swap) go through a
  lock-free SPSC ring; BPM/swing/pattern/metronome/record and pattern cells
  are byte-atomic writes the render task reads directly.
- Silent builds (`USE_AUDIO=0`): the engine compiles as a stub with
  `running()==false` and the sketch drives the same sequencer from
  `Sequencer::tickMillis()` in `loop()` — all serial proof still works.

### Kits

- Builtin kit: 16 sounds synthesized once at boot into PSRAM (~300 KB) —
  sine-sweep kick, noise+tone snare, filtered-noise hats (choke group 1),
  clap, rim, FM percs, bass/chord/vox/fx sounds.
- SD kits: `/mpc/kits/<name>/pad01.wav … pad16.wav`, 16-bit PCM **mono**,
  any sample rate 8-48 kHz. Files keep their native rate — the voice
  resampler converts on the fly (this is the same interpolator that does
  pitch shift, so there is no extra quality cost). Samples clamp at
  `CYPHER_TUNE_MAX_SAMPLE_FRAMES` (2 s @ 48 kHz). Missing pad files get the
  builtin sound so a kit is always fully playable.
- Live swap: the new kit loads into the inactive bank in loop context, the
  engine kills voices and flips banks at a block boundary, then the retired
  bank's buffers are freed. SD I/O never happens on the audio task.

### Touch UI

- 1024x600, manual-flush rendering (`CrowDisplay::begin(..., true)`), dirty
  flags per region + per-cell bitmasks for pads/steps; region flushes are
  row-band `esp_cache_msync` calls. No offscreen canvas — every animated
  element draws once in final state, keeping internal SRAM free for audio.
- Three full-screen views (`TuneUi::View`): performance, settings, and a loops
  browser, dispatched in both touch (`hitTest` / `hitTestSettings` /
  `hitTestLoops`) and render. Routing by view is what stops a stale pad rect
  from firing while another screen is up. SET (step-lane header) opens
  settings; tapping the loop name opens the browser; BACK returns.
- Loops browser: sample packs down the left, that pack's loops in a 2x6 grid
  (12 cells covers the largest pack without paging), NO LOOP button bottom
  left. Stepping through 41 loops with the arrows is not a way to find one, so
  the arrows stay for quick A/B of neighbours and the name became a button.
- Performance layout: transport bar (y0-64: PLAY/STOP/REC, BPM, SWING, MET,
  patterns A-D), 4x4 pad grid left (126x114 px cells, pad 1 bottom-left), right
  column: step grid 2x8, pad-edit panel (VOL/PITCH sliders + choke chips),
  full-width scope/VU, status strip. Theme and kit selection moved to settings
  so this screen only carries what you touch while playing.
- Settings rows: brightness, master volume, theme, kit, idle-dim toggle, plus a
  read-only engine/heap/render block. Brightness is floored at
  `kMinBrightness` (40) — at 0 the panel still renders but shows nothing, which
  looks exactly like a crash and hides the + button. The bar maps the usable
  40..255 range, not 0..255, so full-left still matches the dimmest real state.
- Idle dim (`CYPHER_TUNE_IDLE_DIM_MS`, default 120 s) only fires while the
  transport is **stopped** — dimming mid-loop would punish exactly the case
  where you are listening rather than touching. Touch or a serial byte wakes it.
- Theme, brightness, and the idle-dim flag persist together in NVS
  (`CYPHER_TUNE_NVS_NAMESPACE`); all three are settable headless, so the serial
  path stays a complete control surface.
  Pad cells are wider than tall on purpose: the 4 rows have to fit between the
  transport bar and the full-width status strip (72..560), so `kPadCellH` is
  derived from that budget. Forcing them square overruns the status strip and
  clips the bottom row (regression fixed 2026-07-23).
- Pads fire on press-down; velocity maps from vertical hit position
  (top soft 40 → bottom hard 127). Pressing a pad also selects it for the
  step lane + edit panel (TR workflow, no modes).
- Multi-touch: the shared `CrowDisplay::touchPoints()` API (additive, GT911
  5-point) feeds `TouchTracker`; two pads can be drummed simultaneously.
  Contacts are matched by GT911 track id with a proximity fallback
  (`CYPHER_TUNE_TOUCH_MATCH_RADIUS`) in case ids churn on this panel.
- The UI polls sequencer/bank state each tick and diffs against mirrors, so
  serial commands move the screen with zero extra plumbing; engine events
  (drained once per loop) drive pad flashes and record feedback.
- **Live scope/VU is real audio, not a simulation.** The render task decimates
  the post-clip mix (every 8th frame) into a 256-entry ring and publishes each
  block's peak; `TuneUi` reads both through `PeakFn`/`ScopeFn` callbacks, so
  the UI still includes no audio header. Reads are unlocked — the render task
  can lap a copy mid-draw, which costs at most one visually-odd frame and never
  a crash. The trace redraws on a 40 ms clock (a live waveform can't be
  change-driven); silent builds get 0 samples back and fall back to the
  simulated `VisualVoices` meters rather than drawing a flat line that would
  imply real, dead audio.
- Pad flash is velocity-scaled: the fill blends toward the theme's flash color
  by `velocity x remaining-envelope`, repainted every frame of the 140 ms tail,
  so a ghost note glows and an accent slams. The playhead drags a 3-cell fading
  trail; both the new and previous head's tails are marked dirty on each step so
  the tail clears cleanly.

## Serial Commands

- `help` / `status` / `history`
- `pad <1-16> [vel]` — trigger (records when armed)
- `step <1-16> <pad> [vel]` — toggle, or set explicit velocity
- `vel <step> <pad> <0-127>`
- `bpm <40-240>`, `swing <50-75>` (50 = straight, MPC convention)
- `pat <a|b|c|d>`, `pattern` (prints the 16x16 velocity-bucket grid)
- `play` / `stop` / `record` / `metro [on|off]`
- `gain <pad> <0-255>`, `pitch <pad> <-12..12>`, `choke <pad> <0-4>`
- `kit` / `kit load <name>` / `kit builtin`
- `samples`, `voices`, `engine` (`audio` is an alias)
- `select <pad>` (drive the step lane headless), `touch`, `perf`
- `theme` (report + list) / `theme next` / `theme <name>` (prefix match)
- `bright` / `bright <40-255>` / `bright +` / `bright -`
- `vol` / `vol <0-255>` (master output)
- `settings` / `settings open|close` / `settings idledim`

## Build Flags

Baseline mock/serial path:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
```

The real instrument (display + audio):

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_AUDIO=1" ./scripts/compile-all.sh
```

Add SD kits:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_AUDIO=1 -DUSE_MPC_SD=1" ./scripts/compile-all.sh
```

Flag-matrix rows: `baseline`, `display`, `audio`, `audio-sd`,
`display-audio` (see `scripts/check-flag-matrix.sh`).

Project-local tuning macros (`config/ProjectConfig.h`):

- `CYPHER_TUNE_ENGINE_RATE` (22050) — engine sample rate
- `CYPHER_TUNE_BLOCK_FRAMES` (128) / `CYPHER_TUNE_DMA_DESC` (4) — latency vs
  underrun headroom; raise DESC to 6 if `engine` shows underruns
- `CYPHER_TUNE_VOICES` (8), `CYPHER_TUNE_MASTER_VOLUME` (96)
- `CYPHER_TUNE_MAX_SAMPLE_FRAMES` (96000)
- `CYPHER_TUNE_KIT_DIR` ("/mpc/kits"), `CYPHER_TUNE_SDMMC_1BIT` (1)
- `CYPHER_TUNE_TOUCH_*` — swap/invert/min/max calibration, poll cadence,
  release debounce, contact match radius

## Proof

- `compile-ready`: all five flag rows compile for esp32p4. **Current state.**
- `uploaded`: sketch flashed to a real CrowPanel; static UI verified.
- `field-proven`: pad-to-sound latency (phone slow-mo across finger → LED
  flash → sound), amplifier enable, speaker output, `engine` underruns=0
  after 5 min at `bpm 240` with a dense pattern, and two-finger pad hits all
  verified on the exact board revision and documented with logs or video.

Remaining proof gaps: everything past compile-ready. Native USB-CDC serial
drops once the app runs (see repo docs), so on-hardware proof is
visual/audible plus the pre-drop boot log.
