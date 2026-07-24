# CrowPanel Cypher Vision Cam Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/02-cypher-vision-cam.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/02-cypher-vision-cam/TECHNICAL.md.
```

---

A portable touch camera for the CrowPanel Advanced 7-inch ESP32-P4 (1024x600
MIPI-DSI panel, GT911 touch, SC2336 MIPI-CSI sensor). The P4 owns the camera and
the display; the onboard ESP32-C6 owns the radio.

## Current state

**Stage 1 of 7 — scaffold.** Renamed from the former Vision Guard inspection
kiosk, kiosk sources deleted, flags and serial surface in place, flag matrix
green. No camera, renderer, recorder, stream server, or touch console yet; each
stage-gated command says which stage it lands in rather than failing silently.

| Stage | Scope | State |
|---|---|---|
| 0 | Probe the SCCB bus, confirm sensor address + chip ID | sketch written and compiling; **needs hardware** |
| 1 | Rename, strip, flags, serial scaffold | **done, compile-verified** |
| 2 | `Sc2336Sensor` + `CameraBringup` (CSI + ISP) | **done, compile-verified + linkage-verified** |
| 3 | `CamRenderer` + `VisionCamUi` — PPA blit, live viewfinder | **done, compile-verified + linkage-verified** |
| 4 | ISP tuning + AE/AWB loops + manual exposure | **done, compile-verified + linkage-verified; constants are untuned guesses** |
| 5 | `CamRecorder` — JPEG, SD stills, MJPEG/AVI clips | not started |
| 6 | `CamStreamServer` — soft-AP + MJPEG | not started |
| 7 | Full docs rewrite | not started |

## Why this project exists

The predecessor documented the P4 camera as a permanent stub
(`p4-csi-unavailable-in-arduino`) on the reasoning that `esp32-camera` has no P4
port. The premise is true; the conclusion does not follow, because the P4 does
not use `esp32-camera`. Verified against the installed toolchain — the core
ships two P4 library trees, `esp32p4-libs/3.3.8/` and `esp32p4_es-libs/3.3.8/`,
and both carry the whole stack:

| Capability | Header | Linked archive |
|---|---|---|
| MIPI-CSI controller | `esp_driver_cam/csi/include/esp_cam_ctlr_csi.h` | `libesp_driver_cam.a` |
| ISP (demosaic, AE, AWB, CCM, gamma, sharpen, BF) | `esp_driver_isp/include/driver/isp*.h` | `libesp_driver_isp.a` |
| Hardware JPEG encoder | `esp_driver_jpeg/include/driver/jpeg_encode.h` | `libesp_driver_jpeg.a` |
| Hardware 2D scaler (PPA) | `esp_driver_ppa/include/driver/ppa.h` | `libesp_driver_ppa.a` |
| MIPI PHY power (LDO) | `esp_hw_support/ldo/include/esp_ldo_regulator.h` | `libesp_hw_support.a` |

The whole stack is already in the core. The only hand-written piece is the SC2336
register table.

**This is now demonstrated, not just asserted.** Building the `camera` row and
reading the link map shows the archives genuinely pulled in:

```
libesp_driver_cam.a(esp_cam_ctlr_csi.c.obj)   <- MIPI-CSI receiver
libesp_driver_cam.a(esp_cam_ctlr.c.obj)
libesp_driver_isp.a(isp_core.c.obj)           <- ISP processor
libesp_driver_isp.a(isp_awb.c.obj)
libesp_driver_ppa.a(ppa_srm.c.obj)            <- hardware scale/blit
libesp_driver_ppa.a(ppa_core.c.obj)
```

and the same symbols are **absent** from the `baseline` map, so the flag really
gates them rather than the linker quietly including everything. The camera stack
costs about 26 KB of flash (360,546 → 386,833 bytes); the full
camera + display build is 494,646 bytes, 16% of the app partition.

## Hardware facts

From Elecrow's own
`example/V1.2/idf-code/Lesson13-Camera_Real-Time` (`sdkconfig`, `bsp_camera.h`)
and Espressif's `esp-video-components`:

- Sensor **SC2336**, `CONFIG_CAMERA_SC2336_MIPI_RAW8_1024X600_30FPS=y`
- MIPI-CSI, **2 lanes**, **288 Mbps/lane**, **RAW8 1024x600 @ 30 fps**
- SCCB on **I2C port 1, SCL = IO13, SDA = IO12**
- `reset_pin = -1`, `pwdn_pin = -1` — no GPIO control
- **No host XCLK.** `SC2336_ENABLE_OUT_XCLK` is an empty macro in Espressif's
  driver and `esp_video_init_csi_config_t` has no xclk field; the module
  self-clocks at 24 MHz.
- Sensor ID registers `0x3107` / `0x3108`; SC2336 reports `0xCB3A`

Sensor resolution equals panel resolution, so the fullscreen viewfinder needs no
scaling.

## Bring-up order (load-bearing)

```
SD_MMC  ->  CrowDisplay::begin() [DSI]  ->  CrowCamera::begin() [CSI]
```

- **SD before DSI** is device-proven: mounting SD_MMC after the DSI framebuffer is
  live leaves this panel backlit but blank. See
  `projects/20-pipboy-terminal/src/PipBoyTerminal.cpp`.
- **CSI last**, because `Arduino_ESP32DSIPanel::begin()` already acquires **LDO
  channel 3 at 2500 mV** — the shared `VDD_MIPI_DPHY` rail the CSI PHY also
  needs. Camera bring-up re-acquires it non-adjustably (the LDO driver refcounts
  identical non-adjustable acquisitions), so it is correct either way.

## Planned architecture

```
SC2336 --CSI 2ln/288Mbps--> [CSI ctlr] --RAW8--> [ISP] --RGB565-->
    PSRAM ping-pong buffers (2 x 1.23 MB)
        |-- PPA blit ------> DSI framebuffer (viewfinder rect) --> panel
        |-- HW JPEG encode -> SD (.jpg still / .avi MJPEG clip)
                            \-> C6 MJPEG HTTP stream
```

One JPEG encode per frame is shared by the recorder and the streamer — never
encode twice.

| File | Responsibility |
|---|---|
| `shared/CrowPanelShared/Sc2336Sensor.{h,cpp}` | I2C sensor: ID probe, register table, exposure/gain/flip |
| `shared/CrowPanelShared/CameraBringup.{h,cpp}` | LDO, ISP processor, CSI controller, frame buffers |
| `src/CamPipeline.{h,cpp}` | Frame ownership, AE/AWB control loop |
| `src/CamRenderer.{h,cpp}` | PPA blit into the DSI framebuffer, dirty-rect flush |
| `src/CamRecorder.{h,cpp}` | JPEG encode, SD stills, MJPEG/AVI clips |
| `src/CamStreamServer.{h,cpp}` | Soft-AP, HTTP pages, MJPEG stream |
| `src/VisionCamUi.{h,cpp}` | Four-tab touch console |
| `src/MockCamera.{h,cpp}` | Synthetic frames when `USE_CAMERA_DRIVER=0` |

## Feature flags

| Flag | Effect |
|---|---|
| `USE_CAMERA_DRIVER=1` | Real SC2336 + CSI + ISP. Off → synthetic frames |
| `USE_DISPLAY=1` | Touch console. Off → Serial-only, identical state machine |
| `USE_WIFI=1` | Soft-AP + MJPEG stream server |
| `USE_CAM_SD=1` | SD stills and clips |

`USE_CAMERA_DRIVER` and `USE_DISPLAY` gate **shared library** `.cpp` files, so
they must be real `-D` compiler flags. Defining them in `config/ProjectConfig.h`
alone leaves `CameraBringup.cpp` compiled as no-op stubs and the camera silently
dead — a green build with a dead feature. The same trap applies to
`__has_include` around library headers; see `docs/troubleshooting.md`.

Project-local tuning lives in `config/ProjectConfig.h`: `VISIONCAM_SDMMC_1BIT`,
`VISIONCAM_REC_{WIDTH,HEIGHT,QUALITY,FPS}`, `VISIONCAM_{HTTP,STREAM}_PORT`.
Soft-AP credentials go in a gitignored `config/CamSecrets.h` (copy
`CamSecrets.example.h`).

## Serial commands

115200 baud, line ending **Newline**. Every touch action has a 1:1 command.

- `help` / `status` — shared; `status` also prints the sensor, capture config,
  record targets, and battery state
- `history` — event ring buffer, oldest first
- `cam [status|begin|start|stop|end|grab|exp <n>|gain <n>|ae ...]` — full
  pipeline control. `cam grab` takes one frame and reports its dimensions,
  corner pixels and a sampled mean luma, so a black buffer is distinguishable
  from a real image without a screen attached — the only camera proof available
  headlessly. `cam ae [on|off|target <n>]` drives the exposure loop and is how
  a bad automatic result gets ruled out during bring-up
- `screen [live|gallery|stream|settings]` — show or switch a screen (the tab bar)
- `touch` — raw + mapped touch coordinates, tap count, current screen
- `shot` — capture a still to SD *(stage 5)*
- `rec` — start/stop a clip *(stage 5)*
- `gallery` — list captured media *(stage 5)*
- `stream` — soft-AP and stream state *(stage 6)*
- `selftest` — drive the flow headlessly with explicit `PASS`/`FAIL` lines

## Rendering, and why it is not a normal dashboard

Every other project in this suite repaints a screen and flushes it. This one
cannot, and the reason is worth stating plainly: **the MIPI-DSI panel has a
single framebuffer and no page flip.** Anything drawn becomes visible
immediately, so a clear-blit-chrome-flush loop at video rate tears.

Three rules follow, and `CamRenderer` + `VisionCamUi` exist to enforce them:

1. **The frame never passes through the CPU.** `ppa_do_scale_rotate_mirror`
   DMAs the camera's RGB565 buffer into the framebuffer. A CPU row-copy
   fallback exists for when PPA registration fails, and the Live HUD prints
   `PPA` or `CPU` so a silent fall back to the slow path is visible rather than
   mysterious.
2. **Only the viewfinder rectangle flushes per frame.** `CrowDisplay::flush(x,
   y, w, h)` — the region overload. A full flush would cache-sync 1.2 MB and
   re-push chrome that did not change.
3. **Chrome has its own dirty flag**, repainted on change or a 500 ms
   heartbeat, never in the frame path.

The panel must be built with `CrowDisplay::begin(profile, title, true)` —
`manualFlush=true` — or Arduino_GFX cache-syncs on every single draw call.

Sensor resolution equals panel resolution, so the fullscreen case scales by
exactly 1.0. Image flip is done in the **sensor** (`0x3221`), not the blitter:
the SC2336 mirrors for free and spending PPA bandwidth on it would be waste.

## Build

The local ctags is broken; the `tools.ctags.cmd.path` property is required.

Headless baseline:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" --libraries shared \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/02-cypher-vision-cam
```

Full build:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CAMERA_DRIVER=1 -DUSE_WIFI=1 -DUSE_CAM_SD=1" ./scripts/compile-all.sh
```

Flag matrix rows for this project: `baseline`, `display`, `wifi`, `camera`,
`camera-display`, `sd`, `kitchen-sink`.

```sh
CTAGS_WORKAROUND=1 ./scripts/check-flag-matrix.sh
```

**Verify linkage, not just a green build.** A green compile proves nothing about
whether the camera libraries actually linked:

```sh
grep -c "esp_cam_new_csi_ctlr\|jpeg_new_encoder_engine\|ppa_do_scale_rotate_mirror" _arduino-build/*/02-cypher-vision-cam.ino.map
```

## Upload

```sh
arduino-cli board list
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1 -DUSE_CAMERA_DRIVER=1 -DUSE_WIFI=1 -DUSE_CAM_SD=1" \
  ./scripts/upload-project.sh projects/02-cypher-vision-cam /dev/cu.usbmodem101
```

With `USBMode=hwcdc` the native serial port **drops the moment the app runs**, so
serial monitoring of a running sketch is not viable on this board — diagnostics
render to the panel instead. To reflash, put the board in download mode: hold
BOOT, tap RESET, release BOOT. See `docs/c6-wifi-handoff.md`.

## Image quality, and the gap this project had to fill

The Arduino core ships the ISP's statistics **engines** — the hardware that
measures luminance per block and finds white patches — but **not** `esp_video`'s
software pipeline controller, which is the piece that reads those measurements
and decides what to do. Under ESP-IDF you get that for free. Here it had to be
written; `src/CamPipeline.cpp` is it.

Static tuning is applied once at bring-up in `CameraBringup::configureImagePipeline`:
demosaic (RAW Bayer → RGB, without which there is no colour image at all),
bilateral-filter denoise at a deliberately gentle level 4/20, an sRGB-ish 1/2.2
gamma curve, and a CCM starting at unity. Every stage is best-effort and logs
its own failure — a noisy picture beats no picture.

The two closed loops:

- **Auto exposure.** Reads the AE engine's 5×5 luminance grid, compares the mean
  to a target, and corrects **in the ratio domain** (exposure is multiplicative,
  so the same damping constant behaves identically at both ends of the range).
  Corrections are damped at 0.35 and clamped to 4× per step — undamped, the
  viewfinder visibly pumps. **Exposure moves before gain**, always: a longer
  shutter costs nothing, gain amplifies noise. Gain only rises once the shutter
  hits its ceiling, and drops again as soon as there is light to spare.
- **Auto white balance.** Gray-world over the AWB engine's white-patch sums.
  Because the engine samples *after* the CCM, its estimate is a **residual** to
  fold into the existing gains, not an absolute answer to replace them. Fewer
  than 64 white patches → keep the previous gains, because gray-world on a
  handful of pixels gives a confident wrong answer, and a colour cast that
  wanders as you move the camera is worse than one that sits still.

**The correction is applied through the CCM diagonal, not the WBG block.** WBG's
gain registers are fixed-point and their format is not documented in any public
header, so writing them would be guesswork. The CCM takes a plain float matrix,
and the AWB header itself recommends that route for sensors like the SC2336 that
expose no per-channel RGB gain. The off-diagonal CCM terms are left at zero: a
real colour-correction matrix needs a colour chart and a measurement session, and
an invented one would look worse than none.

> **The AE constants are guesses, not measurements.** The AE engine's luminance
> units are undocumented, so `kDefaultTarget = 110` is a starting point to be
> corrected against a real scene — that is why it is a settable field and why
> `cam ae target <n>` exists. Damping (0.35) and the deadband (6) are the knobs
> to reach for if the loop looks sluggish or hunts. **Manual exposure is the
> committed fallback**: touching the −/+ control drops out of auto, so a
> misbehaving loop can never leave the camera unusable.

## Known risks

Tracked as entries 20-24 in `docs/hardware-risk-register.md`:

1. **AE/AWB is ours to write — and its constants are untuned.** The loops now
   exist (`src/CamPipeline.cpp`), but every constant in them was chosen by
   reasoning, not measured against an image: the luminance target especially,
   since the AE engine's units are undocumented. Expect to tune
   `cam ae target <n>` and possibly the damping on first hardware contact.
   Manual exposure is the committed fallback and is one tap away.
2. **RGB565 byte order.** The CSI controller's `byte_swap_en` may not match what
   the DSI framebuffer expects — Arduino_GFX carrying both `draw16bitRGBBitmap`
   and `draw16bitBeRGBBitmap` is the tell. One-line fix; colour is garbled until
   it is right.
3. **MJPEG throughput over the hosted-C6 SDIO link is unmeasured.** Treat any
   streaming fps figure as unproven until Stage 6 measures it.
4. **Single DSI framebuffer.** No page flip, so a full-screen blit plus chrome
   redraw tears. Mitigated by design: the renderer flushes only the viewfinder
   rect per frame.
5. **No documented battery ADC** on this board. The UI reports battery as
   *unmonitored* rather than estimating a percentage.

## Proof states

- `compile-ready` — **current state, Stage 1 only.** All flag-matrix rows build
  green under the suite FQBN.
- `panel-observed` — **not done.** No CrowPanel is attached to this workspace.
- `field-proven` — **not done.** Requires, in order: sensor ID reads back → live
  viewfinder with measured fps on screen → correct exposure in a bright *and* a
  dim room → a still that opens on a computer → a clip that plays in VLC → a
  phone joining the soft-AP and watching `/stream`.
