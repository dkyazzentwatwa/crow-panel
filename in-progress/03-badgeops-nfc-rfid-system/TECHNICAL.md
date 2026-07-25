# CrowPanel BadgeOps NFC/RFID System Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at in-progress/03-badgeops-nfc-rfid-system.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in in-progress/03-badgeops-nfc-rfid-system/TECHNICAL.md.
```

---

DIY badge check-in, attendance, and lightweight access-control kiosk for the
CrowPanel Advanced 7-inch ESP32-P4 (1024x600 MIPI-DSI + GT911 touch).

The default build is Serial-only (`USE_DISPLAY=0`) and still drives the whole
badge pipeline: simulate a tap, look up the UID, grant or deny it, and log the
decision. Build with `-DUSE_DISPLAY=1` and the same code renders a touch-first
kiosk on the panel. The mock reader drives every screen with **no reader
hardware attached**; the PN532 and MFRC522 drivers stay behind their flags.

## UI

Touch-first dashboard on the 1024x600 DSI panel, dark "ops" palette + FreeSans
fonts, built on the shared `CrowDisplay` bring-up and `Widgets` toolkit. A
`headerBar()` (title, zone, active-reader pill) sits on top and a `tabBar()`
(TAP / REGISTRY / ATTENDANCE / READERS) on the bottom. A `dirty_` flag repaints
only when state changed, then one `CrowDisplay::flush()`; the TAP and RESULT
screens animate at ~8 Hz.

- **Tap** — a reader-state hero: an animated concentric-ring waiting pulse
  around a badge glyph, a large "PRESENT A BADGE", the live reader identity, a
  recap of the last decision, and three quick-tap buttons (active / suspended /
  unknown) so every outcome is reachable by touch.
- **Result** — a full-width granted/denied banner with the badge card (holder,
  role, badge id, UID, zones) and a policy-reason card. Auto-returns to Tap
  after ~3 s; tap anywhere to dismiss sooner.
- **Registry** — a scrollable badge list (status dot + name + role/UID + status
  pill). Tap a row to open its detail card; SIMULATE TAP runs that badge
  through the policy.
- **Attendance** — an audit log of every decision (timestamp, granted/denied,
  holder, UID), newest first, scrollable.
- **Readers** — three reader cards (MOCK / PN532 / MFRC522). The compiled-active
  reader carries an ACTIVE pill and honest ready/wait state; tap a card to
  expand its wiring detail. The live reader is chosen at **compile time**
  (`-DUSE_*_DRIVER`); this screen inspects wiring, it does not hot-swap drivers.

`tick()` reads touch and returns a typed `BadgeOpsEvent` (`EV_TAP_UID`) that the
sketch runs through the same `processTap()` pipeline as the reader and the
serial `tap` command. The UI never mutates the registry, policy, or log itself.

## Touch controls

| Screen | Control | Action |
|---|---|---|
| all | bottom tab bar | switch to TAP / REGISTRY / ATTENDANCE / READERS |
| Tap | TAP ACTIVE / TAP SUSPENDED / TAP UNKNOWN | inject a demo tap (granted / suspended-denied / unknown-denied) |
| Result | tap anywhere in the body | dismiss to Tap (also auto-returns after ~3 s) |
| Registry | a badge row | open that badge's detail card |
| Registry | UP / DOWN | scroll the list |
| Badge detail | SIMULATE TAP | run this badge through the access policy |
| Badge detail | BACK TO LIST | return to the registry |
| Attendance | UP / DOWN | scroll the decision log |
| Readers | a reader card | inspect that reader's wiring detail |

Every touch action has a serial equivalent (`tap`, `screen`, `reader`), so the
whole flow is drivable with no panel attached.

## Serial commands

115200 baud, line ending **Newline**. Every touch action is reachable here too.

- `help` / `status` / `history` — shared (`status` now also prints the active
  reader, its ready state, the current screen, and the decision count)
- `badges` — print the demo badge registry
- `tap [uid]` — simulate a badge tap through the shared pipeline:
  - `tap 04:A1:22:9C` — active technician, access granted
  - `tap C2:44:10:AA` — suspended contractor, denied
  - `tap 11:22:33:44` — unknown badge, denied
- `screen <tap|result|registry|attendance|readers>` — switch screen (mirrors the
  tab bar)
- `reader <mock|pn532|mfrc522>` — inspect a reader's wiring card (the live reader
  is compile-time; this only highlights a card)
- `touch` — print raw + mapped touch coordinates, tap count, and current screen
- `selftest` — drive the mock flow end-to-end headlessly and print an explicit
  PASS/FAIL line per step plus a final `summary N/M` line

### Serial smoke (mock, no panel)

```text
status
selftest
badges
tap 04:A1:22:9C
tap C2:44:10:AA
tap 11:22:33:44
screen registry
screen attendance
reader pn532
touch
history
```

## Security warning

"UID-only RFID/NFC access is suitable for demos, attendance tracking, event
check-in, prototypes, and low-risk internal tools. It should not be treated as
secure access control. Many low-cost RFID/NFC cards and tags can be cloned. For
serious access control, use stronger credential design, signed tokens, backend
validation, secure elements, audit logging, and proper threat modeling."

## Feature flags

Every combination compiles green; the reader paths are gated.

| Flag | Default | Enables |
|---|---|---|
| `USE_DISPLAY` | 0 | the DSI touch kiosk (else headless + serial, identical pipeline) |
| `USE_PN532_DRIVER` | 0 | PN532 NFC reader over I2C (needs Adafruit PN532) |
| `USE_MFRC522_DRIVER` | 0 | MFRC522 RFID reader over SPI (needs MFRC522) |

With no reader flag the mock reader is compiled-active and drives every screen.

## PN532 / MFRC522

Both readers have real, compile-verified (not hardware-verified) scaffolds:

- `src/Pn532Reader.cpp` — PN532 in I2C mode on the touch bus (SDA=45/SCL=46;
  GT911 at 0x5D/0x14, PN532 at 0x24 — no conflict). It refuses to start until
  you set `BADGEOPS_PN532_IRQ`/`RESET` in `config/Pins.h` (copy from
  `Pins.example.h`), so nothing drives a guessed pin.
- `src/Mfrc522Reader.cpp` — MFRC522 over SPI, default SS=10/RST=9 (the
  wireless-socket pins — physically remove any socket module first).

Each reader exposes `ready()`, which the Readers screen and `status` command
surface honestly (mock is always ready; the hardware drivers only report ready
after their chip answered). Enable ONE at a time per
`docs/hardware-bringup-checklist.md` Stage 6, e.g.
`EXTRA_FLAGS="-DUSE_PN532_DRIVER=1"`. Confirm the PN532 module's mode switches
(I2C/SPI/UART) before wiring.

## Compile

From the repository root. The local ctags is broken, so the
`tools.ctags.cmd.path` build property is required.

```sh
# Headless (default, Serial-only) — the baseline build
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  in-progress/03-badgeops-nfc-rfid-system

# Touch kiosk on the panel
arduino-cli compile \
  --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries "$PWD/shared" \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  in-progress/03-badgeops-nfc-rfid-system
```

The suite's `scripts/compile-all.sh` and the `scripts/check-flag-matrix.sh`
rows for this project (`pn532`, `mfrc522`, `both-readers`, `kitchen-sink`) build
the reader and display combinations.

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh in-progress/03-badgeops-nfc-rfid-system /dev/cu.usbmodem101
```

## Proof state

`compile-ready` — the baseline (headless) and `-DUSE_DISPLAY=1` builds both
compile green for the ESP32-P4 target, as do the `pn532`, `mfrc522`,
`both-readers`, and `kitchen-sink` flag-matrix rows. `selftest` passes headlessly
(registry lookups, policy decisions, attendance recording, and screen
navigation).

**Not yet observed on hardware.** No CrowPanel was attached to this session, so
nothing here is device-verified: the five screens have not been seen rendering,
touch zones and the GT911 mapping have not been checked on glass, the animated
waiting pulse has not been watched for tearing, and neither the PN532 nor the
MFRC522 has been proven against a real tag. Treat all of the above as
compile-ready until confirmed on a panel (calibrate touch with the `touch`
command, then set `CROW_TOUCH_*` in `config/ProjectConfig.h` if needed).

## What to film

- Serial boot showing the hardware profile, then `selftest` printing PASS lines.
- The Tap screen's waiting pulse, then the three quick-tap buttons cycling
  granted / suspended-denied / unknown-denied on the Result screen.
- Registry scroll + a badge detail, then SIMULATE TAP.
- The Attendance log filling up.
- The Readers screen and the PN532 vs MFRC522 comparison doc.
