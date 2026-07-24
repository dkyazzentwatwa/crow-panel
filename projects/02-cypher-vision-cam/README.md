# Cypher Vision Cam

A portable touch camera built on the Elecrow CrowPanel Advanced 7-inch display.

Point it, watch a live 1024x600 viewfinder fill the whole panel, tap to shoot,
tap again to record — and let anyone with a phone watch the feed in a browser,
with no router, because the panel hosts its own Wi-Fi.

> This is Project 2 in the [CrowPanel Arduino suite](../../README.md).

## Status

**All seven build stages are written. Nothing has run on a physical panel.**

Every flag combination compiles green for the real ESP32-P4 target, and the link
map confirms the camera, ISP, JPEG and scaler drivers are genuinely pulled in
rather than merely referenced. That is the whole of the evidence.

What has **not** happened: no camera module has been probed, no frame has been
captured, no fps measured, no file written to a card, no clip played back, no
client connected. The auto-exposure loop has never met a real image and its
tuning constants are reasoned guesses. Read everything below as a description of
what the code does, not of what has been observed.

The first thing to run on real hardware is the
[SCCB probe](tools/camprobe/) — it identifies what is actually on the camera
header before any driver trusts an address.

## What replaced what

This project used to be the Vision Guard Inspection Kiosk, which drew a
placeholder viewfinder reading `p4-csi-unavailable-in-arduino` and documented the
camera as a permanent stub. That rested on a wrong conclusion, and correcting it
is why this rebuild exists.

The reasoning that failed went: *`esp32-camera` has no ESP32-P4 port, therefore
the P4 has no Arduino camera path.* The premise is true. The conclusion does not
follow, because the P4 never uses `esp32-camera`. Arduino core 3.3.8 already
ships and links the ESP-IDF camera stack for this exact chip — MIPI-CSI capture,
the image signal processor, a hardware JPEG encoder and a hardware 2D scaler are
all sitting in the core's own P4 libraries with their headers. The only genuinely
missing piece was the SC2336 sensor register table, which is a list of I2C
writes.

Everything the kiosk did — QR scanning, checklists, pass/fail, audit history — is
gone. This is a camera now.

## Hardware

- **Sensor:** SC2336, 2 MP, on the panel's MIPI-CSI camera header
- **Link:** MIPI-CSI, 2 lanes at 288 Mbps, RAW8 **1024x600 @ 30 fps**
- **Control bus:** SCCB (I2C) on SCL = IO13, SDA = IO12, address 0x30
- No reset pin, no power-down pin, no host clock — the module self-clocks

Native sensor resolution is pixel-identical to the panel, so the fullscreen
viewfinder is a 1:1 hardware blit with no scaling at all.

The P4 owns the camera and the screen; the onboard ESP32-C6 owns the radio. The
P4 has no radio of its own and the C6 never touches a pixel.

## Screens

A four-tab touch console:

- **Live** — fullscreen viewfinder. Tap the image to show or hide the HUD, which
  carries shutter, record and start/pause, plus the numbers that actually
  explain performance: measured fps, microseconds per blit, whether the hardware
  blitter or the CPU fallback is running, and the dropped-frame count.
- **Gallery** — what is on the card, with sizes and free space. File rows rather
  than thumbnails, because decoding every JPEG on the card to build a grid would
  stall the render loop for seconds.
- **Stream** — the access point's name, the URL to open, and how many people are
  watching.
- **Settings** — exposure with manual −/+ steps, auto exposure, image flip, and
  battery. The auto-exposure row distinguishes *AUTO* from *AUTO — settling*,
  because an exposure that is still moving is worth knowing about before you
  take a picture. Touching −/+ drops out of auto rather than fighting it.

Every number on screen is measured. The fps figure is counted from frames that
actually arrived, not the sensor's nominal 30. The battery row says
*unmonitored*, because this board documents no way to read the battery and a
made-up percentage would be worse than none.

## Capture

- **Stills** — full sensor resolution, quality 90, written to `/DCIM/CAM_00001.JPG`.
- **Clips** — Motion-JPEG in an AVI container, `/DCIM/VID_00001.AVI`, playable in
  VLC or QuickTime with no post-processing.

Recording defaults to 640x480 at 10 fps, and the reason is worth knowing: 1-bit
SD_MMC sustains roughly 700 KB/s, and full-resolution frames at that rate would
need all of it with nothing to spare. The REC display reports the fps actually
achieved and counts frames the card was too slow to take — if that number climbs,
lower the resolution, the quality or the frame rate.

## Wi-Fi

The panel hosts its own password-protected access point and serves:

- `/` — a viewer page (self-contained; no internet needed to render it)
- `/stream` on port 81 — Motion-JPEG, one viewer at a time
- `/snapshot` — a single still
- `/health` — JSON status

If station credentials are configured it also joins that network, so the feed is
reachable from either side.

## Responsible use

This is a camera that can broadcast what it sees over Wi-Fi. Three things follow,
and all three are enforced rather than suggested:

- **The access point always has a password.** There is no open-AP option and no
  fallback to one: if the configured password is shorter than WPA2's 8-character
  minimum, the radio refuses to start rather than coming up open. If the
  placeholder password from the example config is still in use, the Stream screen
  and `stream` command both say so.
- **One viewer at a time.** A second client would silently halve the first one's
  frame rate.
- **Nothing leaves the device on its own.** Captures go to the SD card you put
  in it. There is no cloud upload, no telemetry, no analytics.

Point it at people only with their knowledge, and check your local law on
recording before you do.

## Technical reference

For build flags, bring-up order, the CSI/ISP pipeline, the AE/AWB loops, the AVI
format, upload commands, known risks, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
