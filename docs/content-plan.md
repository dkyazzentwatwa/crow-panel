# Content Plan

This suite is designed as a three-part creator/developer tutorial.

## Series Arc

1. Start with a CrowPanel product walkthrough and Arduino CLI setup.
2. Show mock mode on Serial so the app behavior is understandable before wiring.
3. Enable one hardware subsystem per episode.
4. Keep the exact board revision warning on screen when discussing radio modules.

## Episode Ideas

- FieldOps: "Build an AIoT field sensor dashboard before wiring the LoRa radio."
- Vision Guard: "Prototype an inspection kiosk with camera and QR workflows stubbed cleanly."
- BadgeOps: "Build an RFID/NFC check-in terminal and explain why UID-only access is not security."

## What To Film

- `arduino-cli board list`
- `./scripts/compile-all.sh`
- Serial monitor output from each mock project
- Switching one config flag and explaining what remains stubbed
- Hardware profile file showing V1.2 pin caution

## Proof Language

Use clear proof states:

- `scaffolded`: files and docs exist
- `compile-ready`: Arduino CLI compile succeeds
- `uploaded`: Arduino CLI upload succeeds to a real port
- `field-proven`: behavior is observed on the real board
