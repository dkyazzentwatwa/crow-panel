# BadgeOps Wiring Notes

No final pins are assigned in this scaffold.

## PN532

PN532 modules may use I2C, SPI, or UART depending on module mode. Check module switches, solder jumpers, and the library example before wiring.

## MFRC522

MFRC522 modules usually use SPI. Pick SS and RST pins only after checking the CrowPanel header and any other active peripherals.

## CrowPanel Headers

Use the CrowPanel I2C, UART, and GPIO headers as the mapping surface. Keep the final mapping in a local pins file derived from `config/Pins.example.h`.
