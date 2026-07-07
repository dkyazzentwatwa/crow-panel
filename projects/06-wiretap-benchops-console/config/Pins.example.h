#ifndef WIRETAP_PINS_EXAMPLE_H
#define WIRETAP_PINS_EXAMPLE_H

// Copy to config/Pins.h for local bench work. Leave entries at -1 until a
// wire is physically connected, labeled, and checked against the active
// CrowPanel hardware profile. The default repo build intentionally probes
// nothing.

// I2C address scan. This sends START/address/STOP clocking only; it does not
// write registers or payload bytes. Use external pullups and 3.3V targets.
#define WIRETAP_I2C_SDA -1
#define WIRETAP_I2C_SCL -1
#define WIRETAP_I2C_CLOCK_HZ 100000

// SPI JEDEC-style ID read. Disabled even in USE_BENCH_PROBES builds unless
// WIRETAP_ALLOW_SPI_ID_CLOCKING is set to 1. It must drive CS/SCK/MOSI to
// send the read-ID opcode and dummy clocks, so only use it on devices whose
// datasheet supports 0x9F-style read ID at the configured voltage and mode.
#define WIRETAP_SPI_SCK -1
#define WIRETAP_SPI_MISO -1
#define WIRETAP_SPI_MOSI -1
#define WIRETAP_SPI_CS -1
#define WIRETAP_SPI_CLOCK_HZ 100000
#define WIRETAP_SPI_READ_ID_OPCODE 0x9F
#define WIRETAP_SPI_DUMMY_BYTE 0x00
#define WIRETAP_ALLOW_SPI_ID_CLOCKING 0

// UART receive only. TX is deliberately not configured by the sketch.
#define WIRETAP_UART_RX -1
#define WIRETAP_UART_BAUD 115200
#define WIRETAP_UART_READ_MS 300
#define WIRETAP_UART_MAX_BYTES 96

// Keep this at 0 unless you are intentionally diagnosing onboard panel pins.
// Display, touch, and audio pins are refused by the GPIO read command by
// default because even INPUT configuration can disturb active peripherals.
#define WIRETAP_ALLOW_PANEL_RESERVED_PINS 0

#endif
