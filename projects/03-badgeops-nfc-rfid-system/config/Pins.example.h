#ifndef BADGEOPS_PINS_EXAMPLE_H
#define BADGEOPS_PINS_EXAMPLE_H

// Copy this file to Pins.h only after confirming your exact module mode,
// CrowPanel board revision, and Elecrow header mapping.

// PN532 modules may use I2C, SPI, or UART depending on module switches or
// solder jumpers. Map the selected mode to CrowPanel I2C/UART/GPIO headers.
#define BADGEOPS_PN532_MODE_TODO "I2C_SPI_OR_UART"
#define BADGEOPS_PN532_SDA_TODO -1
#define BADGEOPS_PN532_SCL_TODO -1
#define BADGEOPS_PN532_SS_TODO -1
#define BADGEOPS_PN532_IRQ_TODO -1
#define BADGEOPS_PN532_RESET_TODO -1

// MFRC522 modules usually use SPI. Choose SS/RST pins only after checking
// which CrowPanel header pins are free in your hardware profile.
#define BADGEOPS_MFRC522_SS_TODO -1
#define BADGEOPS_MFRC522_RST_TODO -1

#endif
