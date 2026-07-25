#ifndef CYPHER_FLOCK_PANEL_PINS_H
#define CYPHER_FLOCK_PANEL_PINS_H

// Initial V1.2 bring-up candidates only. Confirm these pins against the panel
// silkscreen and Elecrow UART example before wiring. They must not collide
// with display, touch, audio, or wireless-socket signals.
//
// ESP32 TX GPIO17 -> CrowPanel RX GPIO48
// ESP32 RX GPIO16 <- CrowPanel TX GPIO47
// GND             <-> GND
// Power all three boards from their own USB connections during first bring-up.

#define FLOCK_UART_RX_PIN 48
#define FLOCK_UART_TX_PIN 47
#define FLOCK_UART_BAUD 115200

#endif
