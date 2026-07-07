#ifndef WIRETAP_PROJECT_CONFIG_H
#define WIRETAP_PROJECT_CONFIG_H

// WireTap is mock-only by default. Build with
//   EXTRA_FLAGS="-DUSE_BENCH_PROBES=1"
// to enable the project-local read-oriented bench probes.
#ifndef USE_BENCH_PROBES
#define USE_BENCH_PROBES 0
#endif

// Local hardware worksheet (gitignored copy of Pins.example.h): bench probe
// pins. Keeping every real pin unset prevents target-driving defaults.
#if __has_include("Pins.h")
#include "Pins.h"
#endif

#ifndef WIRETAP_I2C_SDA
#define WIRETAP_I2C_SDA -1
#endif
#ifndef WIRETAP_I2C_SCL
#define WIRETAP_I2C_SCL -1
#endif
#ifndef WIRETAP_I2C_CLOCK_HZ
#define WIRETAP_I2C_CLOCK_HZ 100000
#endif

#ifndef WIRETAP_SPI_SCK
#define WIRETAP_SPI_SCK -1
#endif
#ifndef WIRETAP_SPI_MISO
#define WIRETAP_SPI_MISO -1
#endif
#ifndef WIRETAP_SPI_MOSI
#define WIRETAP_SPI_MOSI -1
#endif
#ifndef WIRETAP_SPI_CS
#define WIRETAP_SPI_CS -1
#endif
#ifndef WIRETAP_SPI_CLOCK_HZ
#define WIRETAP_SPI_CLOCK_HZ 100000
#endif
#ifndef WIRETAP_SPI_READ_ID_OPCODE
#define WIRETAP_SPI_READ_ID_OPCODE 0x9F
#endif
#ifndef WIRETAP_SPI_DUMMY_BYTE
#define WIRETAP_SPI_DUMMY_BYTE 0x00
#endif
#ifndef WIRETAP_ALLOW_SPI_ID_CLOCKING
#define WIRETAP_ALLOW_SPI_ID_CLOCKING 0
#endif

#ifndef WIRETAP_UART_RX
#define WIRETAP_UART_RX -1
#endif
#ifndef WIRETAP_UART_BAUD
#define WIRETAP_UART_BAUD 115200
#endif
#ifndef WIRETAP_UART_READ_MS
#define WIRETAP_UART_READ_MS 300
#endif
#ifndef WIRETAP_UART_MAX_BYTES
#define WIRETAP_UART_MAX_BYTES 96
#endif

// Display, touch, and audio pins are blocked from GPIO reads unless this is
// explicitly set in Pins.h for a controlled lab diagnostic.
#ifndef WIRETAP_ALLOW_PANEL_RESERVED_PINS
#define WIRETAP_ALLOW_PANEL_RESERVED_PINS 0
#endif

#include <AppConfig.h>

#endif
