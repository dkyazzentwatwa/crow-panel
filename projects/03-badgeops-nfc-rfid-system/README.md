# CrowPanel BadgeOps NFC/RFID System

DIY badge enrollment, check-in, attendance, and lightweight access-control kiosk concept for the CrowPanel Advanced 7-inch ESP32-P4 display.

Default mode is a Serial-only mock demo:

- Simulates badge taps
- Looks up fake badge IDs
- Grants or denies access
- Logs events
- Prints badge terminal state

## Core Screens

- Tap Badge
- Access Granted
- Access Denied
- Enroll Badge
- Badge List
- Event History
- Settings

## Security Warning

"UID-only RFID/NFC access is suitable for demos, attendance tracking, event check-in, prototypes, and low-risk internal tools. It should not be treated as secure access control. Many low-cost RFID/NFC cards and tags can be cloned. For serious access control, use stronger credential design, signed tokens, backend validation, secure elements, audit logging, and proper threat modeling."

## Serial Commands

115200 baud, line ending **Newline**:

- `help` / `status` / `history` — shared commands
- `badges` — print the demo badge registry
- `tap [uid]` — simulate a badge tap through the same pipeline the mock and real readers use:
  - `tap 04:A1:22:9C` — active technician, access granted
  - `tap C2:44:10:AA` — suspended contractor, denied
  - `tap 11:22:33:44` — unknown badge, denied

## Compile

```sh
../../scripts/compile-all.sh
```

The default FQBN targets the real ESP32-P4 (see the root README).

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/03-badgeops-nfc-rfid-system /dev/cu.usbmodem101
```

## PN532 / MFRC522

Both readers have real, compile-verified (not hardware-verified) scaffolds:

- `src/Pn532Reader.cpp` — PN532 in I2C mode on the touch bus (SDA=45/SCL=46; GT911 at 0x5D/0x14, PN532 at 0x24 — no conflict). It refuses to start until you set `BADGEOPS_PN532_IRQ`/`RESET` in `config/Pins.h` (copy from `Pins.example.h`), so nothing drives a guessed pin.
- `src/Mfrc522Reader.cpp` — MFRC522 over SPI, default SS=10/RST=9 (the wireless-socket pins — physically remove any socket module first).

Enable ONE at a time per `docs/hardware-bringup-checklist.md` Stage 6, e.g. `EXTRA_FLAGS="-DUSE_PN532_DRIVER=1"`. Confirm the PN532 module's mode switches (I2C/SPI/UART) before wiring.

## What To Film

- Serial boot showing hardware profile.
- `badges`, then `tap` beats: granted, suspended-denied, unknown-denied.
- `history` replaying the audit trail.
- The security warning.
- The PN532 vs MFRC522 comparison doc.
