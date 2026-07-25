# PN532 vs MFRC522

## PN532

PN532 is usually the more flexible NFC teaching module.

- Can support I2C, SPI, or UART depending on board mode.
- Common in NFC demos.
- Better fit when the lesson is about NFC workflows rather than only cheap card reads.
- Requires careful mode and wiring verification.

## MFRC522

MFRC522 is common, cheap, and usually SPI-based.

- Good for simple RFID card demos.
- Usually needs SS and RST pins in addition to SPI.
- Often used with low-cost tags that should be treated as cloneable.

## Recommendation

Use PN532 for richer NFC tutorials. Use MFRC522 for simple, low-cost RFID demonstrations. In both cases, do not market UID-only checks as secure access control.
