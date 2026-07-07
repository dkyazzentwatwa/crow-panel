# CrowPanel NFC Field Lab / BadgeOps Pro

Combined CrowPanel port inspired by `cypher-pn532` and `cypherbox-mini`.

The default build is mock-first. Real PN532 and MFRC522 paths are compiled only
when their project flags are enabled, and runtime proof still requires the real
CrowPanel, the exact wired reader module, and Serial or display evidence.

## Safety Boundary

UID-only RFID/NFC access is suitable for demos, attendance tracking, event
check-in, prototypes, and low-risk internal tools. It should not be treated as
secure access control. Many low-cost RFID/NFC cards and tags can be cloned. For
serious access control, use stronger credential design, signed tokens, backend
validation, secure elements, audit logging, and proper threat modeling.

The APDU lab is read-only. It only selects the public Type 4 NDEF application
and reads the CC, NLEN, and bounded NDEF preview bytes. It does not select
payment AIDs, proprietary applets, or send write APDUs.

## Project Flags

All flags are passed with `EXTRA_FLAGS` so they reach the sketch and shared
library translation units.

- Default mock mode: no reader flag enabled, uses `MockNfcReader`.
- `USE_DISPLAY=1`: mirrors the Serial state onto the CrowPanel display.
- `USE_PN532_DRIVER=1`: enables the PN532 I2C UID reader and Type 4 NDEF preview.
- `USE_MFRC522_DRIVER=1`: enables the MFRC522 SPI UID reader.
- `NFC_LAB_PN532_IRQ` / `NFC_LAB_PN532_RESET`: required when PN532 is enabled.
- `NFC_LAB_MFRC522_SS` / `NFC_LAB_MFRC522_RST`: default to `10` / `9`.
- `NFC_LAB_MAX_NDEF_PREVIEW_BYTES`: defaults to `48`.

## Wiring Assumptions

PN532 is assumed to be in I2C mode on the CrowPanel touch bus:

- SDA: `45`
- SCL: `46`
- PN532 I2C address: commonly `0x24`
- GT911 touch address: `0x5D` or `0x14`

MFRC522 is assumed to use the wireless-socket SPI pins from the active hardware
profile:

- SCK: `8`
- MISO: `7`
- MOSI: `6`
- SS: `10` by default
- RST: `9` by default

Bring up one physical reader at a time first. If using the wireless socket for
MFRC522, remove any conflicting socket radio module before wiring the reader.
V1.2 wireless pin behavior remains revision-sensitive until field-proven.

## Serial Commands

- `help` / `status` / `history`
- `scan`
- `tap [uid]`
- `ndef`
- `apdu`
- `files`
- `badges`

## Serial Smoke Script

```text
help
status
badges
scan
tap 04:A1:22:9C
tap C2:44:10:AA
tap 11:22:33:44
ndef
apdu
history
```

Expected mock-mode proof:

- `scan` prints a mock UID, type, and reader.
- `tap` prints granted, suspended-denied, and unknown-denied demo decisions.
- `ndef` prints a mock public NDEF URI preview.
- `apdu` prints the safe Type 4 NDEF read trace.

## Compile Checks

From the repo root:

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

## Proof States

- `compile-ready`: the sketch compiles for the selected FQBN and flags.
- `uploaded`: the compiled sketch was uploaded to the detected CrowPanel port.
- `field-proven`: the real CrowPanel, real NFC/RFID reader, and real tag were
  observed through Serial output or display behavior.

Do not claim hardware support from compile success alone.
