# CrowPanel NFC Field Lab / BadgeOps Pro Technical Reference

## AI setup prompt

Copy and paste this prompt into an AI coding assistant from the repository root:

```text
Set up and verify the project at projects/07-nfc-field-lab-badgeops-pro.

Read the repository AGENTS.md first. Preserve this project's existing behavior, safety boundaries, mock-first defaults, and proof-state requirements. Start by inspecting the current source, configuration, and the rest of this technical reference. Do not edit unrelated worktree changes.

Use the documented build and upload commands for this project. Keep credentials, local device settings, and other ignored files out of Git. Do not claim an upload or runtime result unless the exact command succeeded and the behavior was observed on the intended hardware. Report results precisely as compile-ready, uploaded, or field-proven.

At the end, summarize files changed, commands run, and remaining proof gaps. Keep the project README user-facing and put implementation details in projects/07-nfc-field-lab-badgeops-pro/TECHNICAL.md.
```

---

A touch-first NFC/RFID inspection console for the CrowPanel Advanced 7-inch
ESP32-P4 (1024x600 MIPI-DSI, GT911 touch), combined with the BadgeOps
access-decision flow. Inspired by `cypher-pn532` and `cypherbox-mini`.

The UI is a bespoke `NfcLabUi` class built entirely on the shared
`CrowPanelShared` toolkit — `CrowDisplay` bring-up, the `Widgets` drawing kit
(dark "ops" palette, FreeSans fonts, `headerBar`/`tabBar` chrome), and the
`CrowTouch` debounced touch helper. It uses the Adafruit-GFX-style API through
Arduino_GFX. No LVGL. The old generic `OpsDashboard` tile view is gone.

The default build is mock-first: with no reader module wired and only
`-DUSE_DISPLAY=1`, the `MockNfcReader` immediately returns a tag so every screen
fills with believable data and every control responds. Real PN532 and MFRC522
paths compile only when their flags are set, and runtime proof still requires the
real CrowPanel, the exact wired reader, and Serial or display evidence.

## Screens

Five screens, selected from the bottom tab bar (or the `screen` serial command).
`headerBar` shows the title, the active reader, and a persistent `READ-ONLY`
pill; a footer line on every screen restates the boundary.

1. **SCAN** — UID hero card: the tag UID (hero type), tag type, technology and
   usable capacity, plus an active-reader card (driver name, NDEF/Type 4
   capability, tag-present dot) and an at-a-glance strip (NDEF kind, badge
   verdict, file count). The `SCAN` button reads the next tag.
2. **NDEF** — the decoded public NDEF record: a record-type pill, the decoded
   payload (wrapped), byte count and source. Falls back to an honest message on
   a UID-only reader or when no record is present.
3. **APDU** — the read-only Type 4 NDEF exchange as a **stepper**. Command
   (C-APDU) and response (R-APDU) are shown side by side; `NEXT STEP` (or tapping
   the trace) advances one step, `RESET` returns to the first. Six steps: SELECT
   NDEF app, SELECT CC, READ CC, SELECT NDEF file, READ NLEN, READ NDEF. The
   NLEN and payload responses come from the live `SafeApduRead`, and the reader's
   own trace string is shown beneath.
4. **BADGE** — the BadgeOps grant/deny decision on top of the scanned tag: a
   verdict banner (green grant / amber suspended-deny / red unknown-deny), the
   UID being decided, and the matched registry record (name, role, status,
   zones, badge id) or an unknown-badge notice.
5. **FILES** — the on-tag application and file list (NDEF application AID, CC
   file E103, NDEF file E104 with access) plus the exported mock-SD artifacts.

## Touch controls

| Screen | Control | Action | Serial equivalent |
|---|---|---|---|
| any | bottom tab | switch screen | `screen <name>` (or the per-screen command) |
| SCAN | `SCAN` button | read the next tag UID | `scan` |
| APDU | `NEXT STEP` / tap trace | advance the stepper one step (wraps) | `step` |
| APDU | `RESET` | return the stepper to step 1 | `step reset` |

Navigation and buttons key off `CrowTouch::releasedEdge()` + `releaseX/Y()`, so a
drag that starts on one control and ends elsewhere fires nothing.

The UI never mutates application state: `NfcLabUi::tick()` returns a typed
`NfcLabEvent` (`EVT_SCAN_NEXT`, `EVT_APDU_STEP`, `EVT_APDU_RESET`) that the
sketch executes, then repopulates `NfcLabState` from the reader(s).

## Read-only boundary

This is lab inspection, never payment or credential work, and nothing is ever
written to a tag. The APDU path issues only `SELECT` and `READ BINARY`: it selects
the public Type 4 NDEF application and reads the CC, NLEN and a bounded NDEF
preview. It does not select payment AIDs or proprietary applets, and it never
sends a write APDU. The boundary is kept visible in the chrome (`READ-ONLY` pill
+ footer) on every screen.

**UID-only RFID/NFC is not secure access control.** It suits demos, attendance,
event check-in, prototypes, and low-risk internal tools. Many low-cost cards and
tags can be cloned. Serious access control needs stronger credential design,
signed tokens, backend validation, secure elements, audit logging, and proper
threat modeling.

## Serial commands

115200 baud, line ending Newline. Every touch action has a serial equivalent and
every command mirrors the state onto the display.

- `status` / `history` — shared status and event log
- `scan` — read the next tag UID (SCAN screen)
- `tap [uid]` — badge grant/deny; with a UID, decide on that UID (BADGE screen)
- `ndef` — preview the public NDEF record (NDEF screen)
- `apdu` — run the read-only Type 4 trace and print it (APDU screen)
- `step [reset]` — advance the APDU stepper one step, or reset to step 1
- `files` — list the on-tag files and exported artifacts (FILES screen)
- `badges` — print the demo badge registry
- `screen <scan|ndef|apdu|badge|files>` — jump to a screen (tab parity)
- `touch` — raw + mapped touch point, tap count, and current screen
- `selftest` — drive the mock flow end-to-end with PASS/FAIL lines and a summary

## Serial smoke script

```text
help
status
selftest
scan
tap 04:A1:22:9C
tap C2:44:10:AA
tap 11:22:33:44
ndef
apdu
step
step
step reset
files
screen badge
touch
history
```

Expected mock-mode proof:

- `selftest` prints `[selftest] PASS ...` per check and a `summary N/N PASS`.
- `scan` prints a mock UID, type, and reader; repeated `scan` cycles the tags.
- `tap` prints granted, suspended-denied, and unknown-denied demo decisions.
- `ndef` prints a mock public NDEF URI preview.
- `apdu` prints the read-only Type 4 trace; `step` advances the visible step.
- `touch` prints the current screen (touch counters are 0 with no panel).

## selftest

`selftest` runs headlessly (no panel required) and is the functional check:

- readers registered and the reader label is present;
- the four canonical badge decisions (`04:A1:22:9C` grant, `7A:31:90:0D` grant,
  `C2:44:10:AA` suspended-deny, `11:22:33:44` unknown-deny) — static in every
  build;
- on a mock build: a tag scans, a NDEF record decodes, the Type 4 trace and NLEN
  are non-empty, and the badge is evaluated (driver builds print an INFO line
  and skip the tag-dependent checks, since they need a physical tag);
- screen navigation parity across all five screens;
- the APDU stepper bounds (6 steps, wraps to 0).

It ends with `[selftest] summary N/N PASS` (or `FAIL`) and logs the result.

## Project flags

All flags are passed with `EXTRA_FLAGS` so they reach the sketch and the shared
library translation units.

- Default mock mode: no reader flag enabled, uses `MockNfcReader` (drives every
  screen).
- `USE_DISPLAY=1`: the `NfcLabUi` touch console. Without it the project is
  Serial-only and every command still works with identical behavior.
- `USE_PN532_DRIVER=1`: the PN532 I2C UID reader + Type 4 NDEF preview.
- `USE_MFRC522_DRIVER=1`: the MFRC522 SPI UID reader (UID-only; the NDEF/APDU
  screens show the UID-only fallback).
- `NFC_LAB_PN532_IRQ` / `NFC_LAB_PN532_RESET`: required when PN532 is enabled.
- `NFC_LAB_MFRC522_SS` / `NFC_LAB_MFRC522_RST`: default to `10` / `9`.
- `NFC_LAB_MAX_NDEF_PREVIEW_BYTES`: defaults to `48`.

## Wiring assumptions

PN532 is assumed to be in I2C mode on the CrowPanel touch bus:

- SDA: `45`, SCL: `46`, PN532 I2C address commonly `0x24`
- GT911 touch address: `0x5D` or `0x14`

MFRC522 is assumed to use the wireless-socket SPI pins from the active hardware
profile: SCK `8`, MISO `7`, MOSI `6`, SS `10` (default), RST `9` (default).

Bring up one physical reader at a time first. If using the wireless socket for
MFRC522, remove any conflicting socket radio module before wiring the reader.
V1.2 wireless pin behavior remains revision-sensitive until field-proven.

## Build

Use a unique build path so parallel builds do not collide. The local ctags is
broken, so the `tools.ctags.cmd.path` property is required.

Headless (default, `USE_DISPLAY=0`):

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-path _arduino-build/07-baseline \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  projects/07-nfc-field-lab-badgeops-pro
```

Display:

```sh
arduino-cli compile --fqbn "esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600" \
  --libraries shared --build-path _arduino-build/07-display \
  --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1" \
  projects/07-nfc-field-lab-badgeops-pro
```

The supported flag rows (see `scripts/check-flag-matrix.sh`) also build via
`compile-all.sh`:

```sh
CTAGS_WORKAROUND=1 ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_DISPLAY=1" ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_PN532_DRIVER=1" ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_MFRC522_DRIVER=1" ./scripts/compile-all.sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_PN532_DRIVER=1 -DUSE_MFRC522_DRIVER=1" ./scripts/compile-all.sh
```

For runtime PN532 bring-up, add the real IRQ and reset pins, for example:

```sh
CTAGS_WORKAROUND=1 EXTRA_FLAGS="-DUSE_PN532_DRIVER=1 -DNFC_LAB_PN532_IRQ=<pin> -DNFC_LAB_PN532_RESET=<pin>" ./scripts/compile-all.sh
```

## Proof states

- `compile-ready`: the sketch compiles for the selected FQBN and flags.
- `uploaded`: the compiled sketch was uploaded to the detected CrowPanel port.
- `field-proven`: the real CrowPanel, real NFC/RFID reader, and real tag were
  observed through Serial output or display behavior.

Current state: **compile-ready**. The baseline, display, PN532, MFRC522 and
both-readers builds all compile for the ESP32-P4 target. **Not yet observed on
hardware** — no panel is attached to this session, so no screen, touch, or tag
read has been device-verified. Do not claim hardware support from compile
success alone.
