# FieldOps Tutorial

Goal: build the product behavior before touching the SX1262 driver.

## Steps

1. Compile with mock mode.
2. Open Serial Monitor at 115200 baud.
3. Watch fake sensor packets rotate through four nodes.
4. Explain the alert engine and AI summary stub.
5. Open `HardwareProfile.cpp` and point out the V1.2 wireless pin caution.

## Real Hardware Phase

Only after the mock walkthrough works:

- Confirm the board revision.
- Confirm the SX1262 module type.
- Pick the real Arduino library.
- Enable `USE_LORA_DRIVER`.
- Replace placeholders with verified Elecrow example code.
