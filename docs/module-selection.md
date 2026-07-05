# Module Selection

This suite keeps module work explicit and revision-aware.

## FieldOps LoRa / SX1262

Use the SX1262 module path only after confirming the exact Elecrow example and board revision. The official README pin notes include:

- `RADIO_GPIO_CLK`: IO8
- `RADIO_GPIO_MISO`: IO7
- `RADIO_GPIO_MOSI`: IO6
- `SX1262_GPIO_BUSY`: IO9
- `SX1262_GPIO_IRQ`: IO53 in older notes, revision-aware in this repo
- `SX1262_GPIO_NRST`: IO54 in older notes, revision-aware in this repo
- `SX1262_GPIO_NSS`: IO10

## nRF24-Style 2.4 GHz Module

Placeholder official notes:

- `NRF24_GPIO_IRQ`: IO9
- `NRF24_GPIO_CE`: IO53 in older notes, revision-aware in this repo
- `NRF24_GPIO_CS`: IO54 in older notes, revision-aware in this repo

## PN532

PN532 modules may use I2C, SPI, or UART depending on module mode and solder jumpers. Do not assign final pins until you know your module mode.

## MFRC522

MFRC522 modules usually use SPI. Use `Pins.example.h` as the mapping worksheet before enabling `USE_MFRC522_DRIVER`.
