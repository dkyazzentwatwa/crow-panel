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

## Compile

```sh
../../scripts/compile-all.sh
```

Or compile only this sketch:

```sh
arduino-cli compile --fqbn "${FQBN:-esp32:esp32:esp32}" --libraries ../../shared .
```

## Upload

```sh
arduino-cli board list
../../scripts/upload-project.sh projects/03-badgeops-nfc-rfid-system /dev/cu.usbserial-0001
```

## PN532 / MFRC522 Notes

- PN532 wiring may use I2C, SPI, or UART depending on module mode and solder jumpers.
- MFRC522 usually uses SPI.
- Final pins are not assigned here. Use `config/Pins.example.h` as a worksheet and map to CrowPanel I2C, UART, or GPIO headers after verifying the module and board revision.

## What To Film

- Serial boot showing hardware profile.
- Mock badge tap.
- Access granted and denied events.
- The security warning.
- The PN532 vs MFRC522 comparison doc.
