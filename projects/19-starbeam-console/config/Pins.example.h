#ifndef STARBEAM_CONSOLE_PINS_H
#define STARBEAM_CONSOLE_PINS_H

// Copy to Pins.h (gitignored) only when your physical wiring differs from the
// verified CrowPanel header map in ProjectConfig.h.
//
// Shared radio SPI:  SCK=6  MOSI=7  MISO=8
// nRF24 R0..R4 CSN:  9 / 10 / 53 / 54 / 45
// nRF24 R0..R4 CE :  46 / 2 / 3 / 4 / 5
// CC1101 #1: CS=49 GDO0=27      CC1101 #2: CS=28 GDO0=25   (GDO2 n/c)
// Co-proc UART: TX=51  RX=50    Freeze/LED: 52
//
// Power: feed the five nRF24 + two CC1101 VCC pins from a dedicated 3V3
// regulator off the external-header 5V, 10uF+100nF at each module. Leave the
// wireless header's own 3V3 pin unused. Common all grounds.
//
// Strapping: verify IO2 / IO45 / IO46 on the ESP32-P4 datasheet. If IO45 must
// boot low, swap CSN R4 to a spare (e.g. 25) and move a CE onto IO45.

// #define STARBEAM_SPI_SCK 6
// #define STARBEAM_SPI_MOSI 7
// #define STARBEAM_SPI_MISO 8
// #define STARBEAM_NRF0_CSN 9
// #define STARBEAM_NRF4_CSN 45
// #define STARBEAM_CC0_CS 49
// #define STARBEAM_CC0_GDO0 27
// #define STARBEAM_COPROC_TX 51
// #define STARBEAM_COPROC_RX 50

#endif  // STARBEAM_CONSOLE_PINS_H
