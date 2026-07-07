#ifndef BADGEOPS_PINS_H
#define BADGEOPS_PINS_H

// Copy this file to Pins.h (gitignored) and set the pins for the reader
// you actually wired. Confirm the module's mode (I2C/SPI/UART switches or
// solder jumpers) and your CrowPanel header mapping first - see
// docs/hardware-bringup-checklist.md, Stage 6.

// --- PN532 (I2C mode assumed by src/Pn532Reader.cpp) -----------------
// SDA/SCL ride the touch bus (45/46) automatically; only IRQ and RESET
// need free GPIO header pins. The reader refuses to start while IRQ is -1,
// so nothing drives a guessed pin.
// #define BADGEOPS_PN532_IRQ 32
// #define BADGEOPS_PN532_RESET 33

// --- MFRC522 (SPI) ----------------------------------------------------
// Defaults SS=10 / RST=9 reuse the wireless-socket SPI pins: physically
// remove any socket module (SX1262, nRF24) before wiring an MFRC522 there.
// #define BADGEOPS_MFRC522_SS 10
// #define BADGEOPS_MFRC522_RST 9

#endif
