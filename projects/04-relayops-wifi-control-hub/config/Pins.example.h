#ifndef RELAYOPS_PINS_H
#define RELAYOPS_PINS_H

// Copy this file to Pins.h (gitignored) to override on-panel GPIO.
//
// RelayOps has no radio module of its own - Wi-Fi rides the onboard
// ESP32-C6 (esp_hosted). The GPIO it toggles lives on the REMOTE nodes,
// addressed over HTTP (see config/Devices.example.h), not here.
//
// This file is only for a local pin on the CrowPanel itself - e.g. an
// on-board status LED you want to mirror the hub's link state. Pick a FREE
// pin: it must NOT collide with the DSI backlight/reset (IO31/IO41), the
// touch I2C bus (IO45/46/42/40), or the wireless-socket SPI pins. Verify
// against the board silk / Elecrow examples first.
// #define RELAYOPS_STATUS_LED_PIN 48

#endif
