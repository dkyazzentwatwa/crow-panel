# NFC Field Lab / BadgeOps Pro

An NFC and RFID inspection lab for the Elecrow CrowPanel Advanced 7-inch display,
combining a tag-reading bench with the BadgeOps access-decision flow.

A five-screen touch console lets you scan a tag to see its UID, type, and
reader; preview the public NDEF record it carries; step through a read-only APDU
exchange one command at a time; run the same badge grant/deny logic on top; and
browse the tag's file list. Every touch control has a matching serial command.
The default build is fully simulated, so every screen works with no reader
module attached.

> This is Project 7 in the [CrowPanel Arduino suite](../../README.md).

## Status

Compile-ready: the mock, display, PN532, MFRC522, and both-readers builds all
compile for the real ESP32-P4 target. No reader has been wired and no tag has
been tapped on a physical CrowPanel yet, so nothing here is hardware-proven. See
the [technical reference](TECHNICAL.md) for the screen list, touch controls,
wiring assumptions, and the exact bring-up order.

## What you get

- A touch console with five screens: **Scan**, **NDEF**, **APDU**, **Badge**,
  and **Files**, with a bottom tab bar and a persistent read-only banner
- UID scanning with tag type, technology, and the active reader shown
- An NDEF preview of the public record on a tag
- A step-by-step APDU stepper for a read-only Type 4 NDEF exchange, command and
  response side by side, one tap per step
- The BadgeOps grant, suspended-deny, and unknown-deny decision flow
- The on-tag file/application list and a browsable badge registry
- Two reader options behind flags: PN532 over I2C, or MFRC522 over SPI
- A `selftest` command that drives the whole mock flow with PASS/FAIL output
- An offline demo covering every command with simulated tags

## Where the lab stops

The APDU path is read-only and narrow on purpose. It selects the public Type 4
NDEF application and reads the capability container, the record length, and a
bounded preview of the NDEF bytes. That is all. It does not select payment AIDs
or proprietary applets, and it never sends a write APDU.

## Security warning

**UID-only RFID/NFC is not secure access control.** It suits demos, attendance
tracking, event check-in, prototypes, and low-risk internal tools. Many low-cost
cards and tags can be cloned. Anything that actually protects something needs
stronger credential design, signed tokens, backend validation, a secure element,
audit logging, and real threat modeling.

## Responsible use

Read tags you own or are authorized to inspect. This project does not clone
cards, emulate credentials, brute-force keys, or touch payment applications.

## Technical reference

For installation, build flags, configuration, upload commands, device details,
file layout, troubleshooting, safety boundaries, and proof terminology, see
[TECHNICAL.md](TECHNICAL.md).
