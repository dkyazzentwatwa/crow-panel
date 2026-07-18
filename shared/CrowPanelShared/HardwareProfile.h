#ifndef CROW_PANEL_HARDWARE_PROFILE_H
#define CROW_PANEL_HARDWARE_PROFILE_H

#include <Arduino.h>
#include "AppConfig.h"

struct TouchPins {
  uint8_t scl;
  uint8_t sda;
  uint8_t interruptPin;
  uint8_t resetPin;
  uint8_t addressWhenIntHigh;
  uint8_t addressWhenIntLow;
};

struct WirelessPins {
  uint8_t spiClk;
  uint8_t spiMiso;
  uint8_t spiMosi;
  uint8_t sx1262Busy;
  uint8_t sx1262Irq;
  uint8_t sx1262Reset;
  uint8_t sx1262Nss;
  uint8_t nrf24Irq;
  uint8_t nrf24Ce;
  uint8_t nrf24Cs;
};

struct AudioPins {
  uint8_t lrclk;
  uint8_t bclk;
  uint8_t sdata;
  uint8_t control;
  uint8_t micClk;
  uint8_t micDin;
  bool controlActiveHigh;
};

struct DisplayPins {
  uint8_t backlight;  // LEDC PWM, active high
  uint8_t lcdReset;
};

// P4 <-> onboard ESP32-C6 SDIO link (esp_hosted). The core's prebuilt defaults
// assume Espressif's Function-EV board (D0=14..D3=17, reset 54); the CrowPanel
// wires the data lines REVERSED with the C6 reset on IO32 (verified against
// Elecrow's V1.2 schematic + their C6 upgrade guide). HostedWiFi applies these
// pins before any repo WiFi path starts esp_hosted - without it, Wi-Fi can never
// come up and esp_hosted's failed init reboots the board.
struct HostedSdioPins {
  int8_t clk;
  int8_t cmd;
  int8_t d0;
  int8_t d1;
  int8_t d2;
  int8_t d3;
  int8_t reset;  // C6 enable/reset GPIO
};

// MIPI-DSI panel timings (EK79007 controller, 1024x600 RGB565).
struct DisplayTiming {
  uint16_t hsyncPulse;
  uint16_t hsyncBackPorch;
  uint16_t hsyncFrontPorch;
  uint16_t vsyncPulse;
  uint16_t vsyncBackPorch;
  uint16_t vsyncFrontPorch;
  uint32_t preferSpeedHz;    // DPI pixel clock
  uint32_t laneBitRateMbps;  // per-lane MIPI-DSI bit rate (2 lanes)
};

struct HardwareProfile {
  int id;
  const char *name;
  TouchPins touch;
  WirelessPins wireless;
  AudioPins audio;
  DisplayPins display;
  DisplayTiming displayTiming;
  HostedSdioPins hostedSdio;
  const char *revisionNote;
};

const HardwareProfile &activeHardwareProfile();
const HardwareProfile &profileFor(int profileId);
void printHardwareProfile(Stream &out, const HardwareProfile &profile);

#endif
