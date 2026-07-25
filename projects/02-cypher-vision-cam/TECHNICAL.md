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

**Field-proven on a V1.2 panel, 2026-07-24.** Measured, not estimated:

| Path | Result |
|---|---|
| SC2336 identification | address `0x30`, chip id `0xCB3A` |
| Viewfinder | ~21 fps, 1024x600, correct colour, `byte_swap_en = false` |
| Stills / clips to SD | JPEG opens on a computer; AVI plays **and seeks** in VLC |
| BOOT button shutter | works |
| MJPEG stream (station mode) | **15–20 fps** at 640x480 q60, panel held ~21 fps concurrently |

**Not verified:** soft-AP association (advertises, never associates — station
mode is the proven path), and AE convergence across a wide brightness range.

### What bring-up actually cost

Five real bugs surfaced only against hardware. They are worth reading as a set,
because four of the five looked like something other than what they were:

1. **Missing black-level correction** — hazy frames with grey blacks. BLC was
   simply never configured; it is the first stage of the pipeline for a reason.
2. **AWB window too narrow to see its own distortion** — the R/G and B/G
   acceptance bounds were a "sensible" 0.6–2.0, but raw Bayer is green-heavy, so
   an uncorrected frame fell outside them. Zero white patches, gains frozen at
   unity, and the green cast the loop existed to remove was exactly what blinded
   it. *A corrective loop must be able to observe the distortion it corrects.*
3. **Blocking ISP statistics reads** — two 120 ms-timeout reads per 200 ms
   window. Average frame rate looked healthy at 20–30 fps, so nothing seemed
   wrong; but taps landing inside a stall were swallowed and the **touchscreen
   appeared dead**. *Averages hide worst cases, and interactive latency lives in
   the worst case.*
4. **Per-loop HTTP handler registration** — `WebServer::on()` appends rather
   than replaces, so re-registering a route each loop grew an unbounded handler
   list at 20–30 entries per second.
5. **Stream URL hardcoded to the AP address** — the panel is reachable at two
   addresses at once, so a LAN viewer loaded the page fine and was then pointed
   at an AP address it had no route to. Video blank, `/snapshot` fine.

Plus one that was not a bug at all: the **soft-AP SSID had been set to the same
name as the home network**, so clients "joined" it and silently associated with
the stronger router instead. The panel reported zero stations and looked broken
while working perfectly.

### Original stage breakdown

| Stage | Scope | State |
|---|---|---|
| 0 | Probe the SCCB bus, confirm sensor address + chip ID | written, compiles; **needs hardware** |
| 1 | Rename, strip, flags, serial scaffold | compile-verified |
| 2 | `Sc2336Sensor` + `CameraBringup` (CSI + ISP) | compile- + linkage-verified |
| 3 | `CamRenderer` + `VisionCamUi` — PPA blit, live viewfinder | compile- + linkage-verified |
| 4 | ISP tuning + AE/AWB loops + manual exposure | compile- + linkage-verified; **constants untuned** |
| 5 | `JpegEncoder` + `CamRecorder` — stills, MJPEG/AVI clips | compile- + linkage-verified |
| 6 | `CamStreamServer` — soft-AP + MJPEG | compile- + linkage-verified |
| 7 | Docs | done |

**What "verified" means here, precisely.** Every flag combination builds green,
and the link map shows the ESP-IDF camera, ISP, JPEG and PPA archives genuinely
pulled in (and absent when the flags are off). That is the entire body of
evidence. No sensor has been probed, no frame captured, no file written, no clip
played, no client connected. Treat every performance figure in this document as
a target or an estimate, never a measurement.

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
| `shared/CrowPanelShared/CameraBringup.{h,cpp}` | LDO, ISP processor + tuning, CSI controller, frame buffers, AE/AWB statistics |
| `src/CamPipeline.{h,cpp}` | AE/AWB control policy |
| `src/CamRenderer.{h,cpp}` | PPA blit into the DSI framebuffer, dirty-rect flush |
| `src/JpegEncoder.{h,cpp}` | Hardware JPEG + PPA down-scale, shared by recorder and stream |
| `src/CamRecorder.{h,cpp}` | SD stills, MJPEG/AVI clips with index |
| `src/CamStreamServer.{h,cpp}` | Soft-AP, HTTP pages, MJPEG stream |
| `src/VisionCamUi.{h,cpp}` | Four-tab touch console |
| `tools/camprobe/` | Standalone SCCB probe — run this first on new hardware |

`JpegEncoder` exists because the recorder and the stream server need exactly the
same operation and the JPEG peripheral is a single hardware block with large
working buffers. One instance is owned by the sketch and passed to both; a second
would be pure waste. Neither is reentrant, and both run from `loop()`, so single
ownership is the whole synchronisation story.

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
- `shot` — capture a still to SD (queued for the next frame, same path as the
  touch shutter)
- `rec [start|stop]` — record a clip; no argument toggles
- `gallery` — list `/DCIM` with sizes and free space
- `stream [on|off]` — soft-AP and MJPEG server state
- `selftest` — drive the flow headlessly with explicit `PASS`/`FAIL` lines

### Serial smoke (no panel, no card, no camera)

Every one of these works in the baseline build and reports honestly when the
hardware it needs is absent:

```text
status
cam status
cam begin
cam grab
screen live
shot
rec start
rec stop
gallery
stream
selftest
```

## The web app

Four pages, all self-contained — no CDN, no webfonts, no frameworks. The panel is
frequently its own island with no route to the internet, so anything fetched from
elsewhere would simply not arrive. The palette is copied from
`shared/CrowPanelShared/DashboardWidgets.h` so browser and panel read as one
product.

| Route | Serves |
|---|---|
| `GET /` | Live view, Take photo / Record buttons, self-updating status line |
| `GET /gallery` | Lazy-loaded thumbnail grid + download links |
| `GET /media?f=NAME` | One file: JPEG inline, AVI as an attachment |
| `POST /shutter` | Raise the shutter flag |
| `POST /record` | Toggle recording |
| `GET /health` | JSON: fps, recording, viewer, file count, free MB |

Three decisions worth knowing:

**The stream URL comes from the request's `Host` header, never from
`softAPIP()`.** The panel can be reachable at two addresses at once (AP and
station). Baking in the AP address produced a page that loaded perfectly over the
LAN and then pointed the browser at an address it had no route to — video blank,
`/snapshot` fine, because a relative path follows the host and an absolute one
does not.

**`loading=lazy` on gallery thumbnails is load-bearing.** There are no stored
thumbnails, so each tile is the full ~200 KB JPEG. A card of 60 stills would pull
~12 MB at once over the same link carrying the video. Lazy loading is what makes
the page usable rather than a nicety.

**Control endpoints only raise flags.** `POST /shutter` sets the same
`shutterRequested` the touch button and BOOT button use; the capture happens in
`loop()`, the one place holding a live frame. A second path owning a camera
buffer would break the acquire/release contract.

### The download freeze, stated plainly

`WebServer::streamFile()` runs to completion inside `handleClient()`. While it
does, the render loop is stopped: no video, no touch. A still is a blink; a
30 MB clip is most of a minute.

Rather than hide this, `serveMedia_` sets a flag before the transfer, drops any
MJPEG viewer (it cannot run anyway, and releasing it frees the link), and the
panel draws a `SERVING FILE` overlay. A frozen panel that says why is a very
different experience from one that appears to have crashed.

**The fix, if it becomes annoying, is a chunked transfer driven from `loop()`** —
the same shape as the MJPEG pusher, a slice per iteration. Contained, not a
rewrite. It was not done up front because the simple version is a fraction of the
code and stills — the common case — are unaffected.

### Path traversal

`/media?f=` turns a network-supplied name into a path on the card, so
`validMediaName_` whitelists rather than blacklists: exactly 13 characters,
`CAM_` or `VID_`, five digits, matching extension. Checking for `..` and slashes
would be the start of an arms race; requiring the precise shape this recorder
produces leaves nothing to negotiate. Verified against `../../secret`,
`/etc/passwd`, `../CAM_00001.JPG`, `CAM_00001.JPG/..`, wrong digit counts, and
case variations — all rejected.

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

Image flip is done in the **sensor** (`0x3221`), not the blitter: the SC2336
mirrors for free and spending PPA bandwidth on it would be waste.

### The Live screen is full-bleed, and that is a correctness fix

The viewfinder rect is the entire panel — `0, 0, 1024, 600` — which makes the
PPA's `scale_x` and `scale_y` both exactly 1.0. Every sensor pixel lands on one
panel pixel.

This replaced a 1024x464 rect that fitted neatly between the header and tab bars.
It looked reasonable and was quietly **wrong**: the PPA scaled x by 1.00 and y by
464/600 = 0.77, squashing the preview 23% vertically while captured stills — taken
from the full frame — came out correctly. **A preview that disagrees with the
capture is worse than an ugly one**, and nothing in the code complained, because
asking the PPA for a non-uniform scale is a perfectly legal request.

Consequences for chrome: Live has no `headerBar` and no `tabBar`. A single 76 px
bar is drawn **on top of** the image after the frame blit, carrying the actions,
a compact status cluster, and navigation to the other three screens. Tapping the
image hides it; a corner readout stays so a clean viewfinder is not a blind one.
The other screens are dashboards, not video, and keep the conventional chrome.

The bar is a solid fill rather than a translucent wash: the framebuffer is RGB565
with no alpha, so real transparency would mean reading back and blending 76k
pixels every frame.

**Flush order is what stops the bar flickering.** `CamRenderer::drawFrame` takes
an `autoFlush` flag, and Live passes `false`: the PPA writes all 600 rows into
the framebuffer, the bar is drawn over the bottom 76, and then the image region
and the bar region are flushed separately. Letting `drawFrame` flush the full
panel first pushed video into the bar's rows and the bar over it a moment later —
every frame, twenty times a second, which reads as the status line strobing. The
framebuffer content was always correct; only the push order was wrong.

## On-panel image preview

Tapping a still in Gallery decodes and displays it, using the same JPEG block as
the capture path but in the other direction: SD → `jpeg_decoder_process` (RGB565)
→ PPA scale → framebuffer. `src/ImageViewer.{h,cpp}`.

`jpeg_decoder_get_info` reads the header **before** decoding, so the output size
is known and checked against the buffer first — a mismatch there is the
difference between a picture and heap corruption.

The image is letterboxed to preserve aspect rather than stretched to fill.
Showing a photo at the wrong shape is precisely the bug the viewfinder rework
fixed; reintroducing it in the viewer would be careless.

Displaying is a modal state: `tick()` returns early while an image is up, so
nothing paints over it, and the dismissing tap is swallowed before any other
hit-test. Clips are not tappable — there is no video player here, and an
affordance that does nothing is worse than none.

## Portrait orientation

The insight that makes this cheap: **the camera and panel are on the same device
and turn together**, so the viewfinder needs no pixel rotation at all. Turning
the panel gives a correct, full-screen, taller framing for free. Only two things
actually break, and only those two are fixed:

| Breaks | Fix |
|---|---|
| Chrome reads sideways | `Arduino_GFX::setRotation(1)` — logical canvas becomes 600x1024 |
| Saved files open rotated | `JpegEncoder::setRotation(90)` — PPA rotates before encode, giving a true 600x1024 file |

Consequences worth knowing:

**Layout is runtime, not compile-time.** `VisionCamUi.cpp` holds a `Layout`
struct filled by `computeLayout(portrait)`; every position reads from it. Hit
rects and draw calls use the *same fields*, so the two can only disagree if a
field is wrong for both.

**Portrait cannot use the shared chrome.** `headerBar`/`tabBar` hardcode
`kChromeW = 1024` (`DashboardWidgets.h`), so they would draw off the edge of a
600-wide canvas. Portrait draws its own header and tab strip; landscape still
uses the shared helpers.

**Touch must be remapped by hand.** `CrowTouch` reports panel coordinates and
knows nothing about rotation, so `mapTouch_` exchanges the axes in portrait.
Rotating the drawing but not the touch would put every control visibly in one
place and tappable in another — worse than no rotation at all.

**Two layouts, two sets of constraints.** Settings has eight rows: fine in
portrait's 1024 px of height, but 156 px too tall for landscape, so landscape
uses two columns of four. The Live bar is one row landscape, three rows portrait
(600 px will not hold shutter, two actions and three nav targets side by side at
a size worth aiming at). Both were verified arithmetically against the content
area before flashing, not discovered on the panel.

## Sound

`src/CamAudio.{h,cpp}`. Cues are synthesized — a filtered noise burst under a
fast decay for the shutter, triangle two-tones for record start/stop — so there
is no asset to ship or find, and they work with no card inserted, which is
exactly when the error tone matters.

Two details are inherited from project 09's field-proven `AudioEngine` rather
than rediscovered: **the amp enable is active LOW** on this board (it comes from
`profile.audio.controlActiveHigh` and is never hardcoded — driving IO30 high
mutes the speaker while I2S keeps streaming, which presents as "the code works
but there is no sound"), and **silence must be streamed before raising the
enable** or the amp wakes onto an undefined bus and pops.

Playback runs on its own FreeRTOS task pinned to core 0. Writing a 120 ms cue to
I2S from `loop()` would stall the render loop for 120 ms — the same mistake as
the ISP statistics stalls. `play()` posts to a 4-deep queue and returns; a fifth
queued cue is dropped, because a backlog of shutter clicks is worse than a
missing one.

## Persisted settings

Image flip (`vflip` / `hmirror`), panel orientation (`portrait`) and the sound
toggle (`sound`) are written to NVS via `Preferences`. Flip is restored
**before** the sensor starts streaming, so the first frame is already the right
way up rather than flipping a moment after boot.

It lives in NVS rather than on the card because it must survive with no card
present, and it is two bits. `StorageManager` is in-memory only, so this uses
`Preferences` directly. Orientation describes how the camera is *mounted* — a
physical fact that does not change between power cycles — so asking the user to
re-set it each boot would be asking them to correct the same thing repeatedly.

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

## Capture, and the constraint that shapes it

**SD write speed is the binding constraint, not the encoder.** 1-bit SD_MMC
sustains roughly 700 KB/s. A full-resolution 1024x600 frame at q75 is about
70 KB, so recording natively at 10 fps would consume the entire budget with
nothing left for filesystem overhead. Frames are therefore hardware-scaled to
the record size (640x480 by default) before encoding.

Stills are different and go at full sensor resolution, quality 90 — the budget
that shapes video does not apply to one file.

Clips are **Motion-JPEG in an AVI container**: a RIFF header, one `00dc` chunk
per JPEG frame, and an `idx1` index appended at close. That is the format every
player already understands, and hand-writing it is far less work than fitting a
container library into an Arduino sketch. Without the index a player can show
the clip but cannot seek in it, and some refuse to open it at all.

Two details in the AVI writer are worth knowing because both were bugs first:

- **Byte offsets are tracked by hand, not read from `File::size()`.** For a file
  open for writing, `size()` reflects what has been flushed, not what has been
  buffered — using it to compute chunk offsets silently corrupts the index.
- **The header is patched in a second pass, reopened as `"r+"`.** The clip is
  written with `FILE_WRITE` (`"w"`), a truncating write-only mode; seeking
  backwards in it to backfill sizes would be relying on undefined behaviour.

The header layout is locked down with `static_assert`s on the RIFF LIST
arithmetic. That is not decoration: the `hdrl` size was written four bytes short
during development, which produces a file most players still open (they rescan
on failure) but whose header is malformed. Compile-time checks make that class
of error impossible to reintroduce.

The REC display reports the fps **actually achieved** and counts frames the card
was too slow to accept. Frames arriving faster than the record rate are skipped
and deliberately *not* counted as drops — conflating "not wanted" with "could
not keep up" would make a healthy recording look like a failing one.

## Streaming

Two sockets, and the split is not arbitrary:

| Port | Server | Serves |
|---|---|---|
| 80 | `WebServer` | `/` viewer page, `/snapshot`, `/health` |
| 81 | `WiFiServer` | `/stream` — `multipart/x-mixed-replace` |

`WebServer::handleClient()` runs a request to completion before returning, and
an MJPEG response never completes. Serving the stream from port 80 would stall
the render loop permanently. The raw socket on 81 lets the pusher hand over one
frame per `loop()` iteration and return immediately.

**One viewer at a time**, by design: a second client would multiply both the
encode cost and the SDIO traffic to the C6, and silently halving the first
viewer's frame rate is worse than being told the seat is taken. A short socket
write drops the viewer rather than sending a truncated frame, which would
desynchronise the multipart stream permanently.

Stream frames are 640x480 at q60, rate-limited to 10 fps independently of the
camera — the viewfinder can run faster than the link, and pushing every frame
would only build a backlog in the C6.

`configureCrowPanelHostedWiFiPins()` **must** be called before any `WiFi.*` call:
this panel wires the P4↔C6 SDIO data lines differently from the core's default
map, and without the remap `esp_hosted` hangs during handshake and the board
watchdog-reboots. See `docs/c6-wifi-handoff.md`.

### The AP always has a password

There is no open-AP path and no fallback to one. If the configured password is
shorter than WPA2's 8-character minimum, `begin()` refuses to start the radio and
says why. If the placeholder from `CamSecrets.example.h` is still in use, both
the Stream screen and `stream` report it. This device broadcasts a live camera
feed; an open AP would put it in range of anyone.

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

- `compile-ready` — **current state.** All seven flag-matrix rows build green
  under the suite FQBN, and the camera/ISP/JPEG/PPA archives are confirmed in
  the link map.
- `panel-observed` — **not done.** No CrowPanel is attached to this workspace.
- `field-proven` — **not done.** Requires, in order:
  1. `tools/camprobe` reports `SC2336 present` and its address
  2. `cam grab` returns plausible dimensions and a non-zero mean luma
  3. Live viewfinder renders, HUD shows `PPA` (not `CPU`), fps recorded
  4. Correct exposure in a bright room **and** a dim one
  5. A still that opens on a computer
  6. A clip that plays in VLC and seeks correctly
  7. A phone joins the soft-AP and watches `/stream`; stream fps recorded

Steps 3, 6 and 7 each produce a number this document currently only estimates.
Record them.
