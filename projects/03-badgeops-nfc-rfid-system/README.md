# BadgeOps NFC/RFID System

A badge enrollment, check-in, and attendance kiosk for the Elecrow CrowPanel
Advanced 7-inch display.

Tap a badge, get a clear granted or denied screen, and keep an audit trail of
every decision. It runs as a complete offline demo with a registry of fictional
badges — active, suspended, and expired — so you can exercise every outcome
without a reader attached, from the touch screen or over Serial.

> This is Project 3 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-verified only: the headless and touch-display builds compile green for
the real ESP32-P4 target, but nothing has been observed running on a physical
CrowPanel. The five screens have not been seen on glass, touch has not been
checked, and both reader scaffolds — written and building — have not been proven
against a real tag. See the [technical reference](TECHNICAL.md) for the screen
list, touch controls, serial commands, wiring, and the staged bring-up order.

## What you get

- A touch kiosk with five screens — Tap, Result, Registry, Attendance, Readers —
  plus a bottom tab bar to move between them
- A tap screen with an animated waiting state and distinct granted/denied results
- A scrollable badge registry you can inspect badge by badge
- A decision log that doubles as an attendance and audit trail
- A Readers screen that shows the mock / PN532 / MFRC522 wiring and which reader
  is compiled in
- Two reader options behind flags: PN532 over I2C, or MFRC522 over SPI
- A full offline demo covering active, suspended, and unknown-badge paths — no
  reader hardware required, drivable by touch or over Serial

## Security warning

**UID-only RFID/NFC is not secure access control.** It is fine for demos,
attendance tracking, event check-in, prototypes, and low-risk internal tools.
Many low-cost cards and tags can be cloned in seconds. Anything that actually
protects something needs stronger credential design, signed tokens, backend
validation, a secure element, audit logging, and real threat modeling.

Read [`docs/security-notes.md`](../../docs/security-notes.md) before using
RFID/NFC language in public material about this project.

## Responsible use

This project reads badge identifiers you present to it. It does not clone cards,
emulate credentials, or attack readers. The badge registry and event log stay on
the panel unless you deliberately wire them to a backend.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
