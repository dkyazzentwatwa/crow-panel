# BadgeOps Tutorial

Goal: build a badge terminal demo without pretending UID-only RFID/NFC is secure access control.

## Steps

1. Compile in mock mode.
2. Open Serial Monitor at 115200 baud.
3. Watch simulated badge taps.
4. Show registry lookup.
5. Show granted, denied, and suspended badge outcomes.
6. Read the security warning out loud.

## Real Hardware Phase

Start by choosing a reader:

- PN532 if you want NFC flexibility and possible I2C/SPI/UART modes.
- MFRC522 if you want a cheap SPI RFID demo.

Then map pins from the real module mode to available CrowPanel headers.
