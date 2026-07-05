# FieldOps Wiring Notes

The SX1262 path is not wired by default.

Official placeholder pins are documented in `docs/module-selection.md` and `shared/CrowPanelShared/HardwareProfile.cpp`.

## V1.2 Caution

The official README says V1.2 changed the wireless module socket pin allocation. Do not solder, jumper, or publish a final wiring diagram from this scaffold alone.

## Before Wiring

- Identify the exact CrowPanel hardware revision.
- Identify the exact LoRa/SX1262 module.
- Compare against the Elecrow example for that module.
- Update a local pins file from `config/Pins.example.h`.
