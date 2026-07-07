#ifndef FIELDOPS_PINS_H
#define FIELDOPS_PINS_H

// Copy this file to Pins.h (gitignored) to override LoRa radio parameters.
// SX1262 PINS ARE NOT SET HERE - they come from the active HardwareProfile
// in shared/CrowPanelShared/HardwareProfile.cpp, selected by
// CROWPANEL_HARDWARE_PROFILE. Confirm your board revision first
// (docs/hardware-bringup-checklist.md, Stage 2).
//
// Radio defaults (in src/LoRaGateway.cpp) mirror Elecrow's Lesson13
// example: 915.0 MHz, BW 125 kHz, SF7, CR7, private sync word, 22 dBm,
// preamble 8, TCXO 1.6 V.

// EU boards: 868 MHz. Transmitting on the wrong band is a regulatory issue.
// #define FIELDOPS_LORA_FREQ_MHZ 868.0

// Longer range at the cost of airtime: raise the spreading factor.
// #define FIELDOPS_LORA_SF 9

// #define FIELDOPS_LORA_BW_KHZ 125.0
// #define FIELDOPS_LORA_POWER_DBM 22

// --- ESP-NOW bridge UART (only used with -DUSE_ESPNOW=1) ---
// The panel reads sensor/presence frames over Serial1 from the ESP-NOW<->UART
// bridge (see espnow/README.md). Set RX/TX to two FREE pins on the CrowPanel
// GPIO header. VERIFY against the board silk / Elecrow UART example first -
// they must NOT collide with the DSI backlight/reset (IO31/IO41), the touch
// I2C bus (IO45/46/42/40), or the wireless-socket SPI pins. Wiring: bridge
// TX -> panel RX, bridge RX -> panel TX, GND <-> GND.
// #define ESPNOW_UART_RX 48
// #define ESPNOW_UART_TX 47
// #define ESPNOW_UART_BAUD 115200

#endif
