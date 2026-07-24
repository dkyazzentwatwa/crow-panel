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

// MIPI-CSI camera header (SC2336 sensor on the CrowPanel Advanced 7").
//
// Values come from Elecrow's own IDF example, example/V1.2/idf-code/
// Lesson13-Camera_Real-Time (peripheral/bsp_camera/include/bsp_camera.h for the
// SCCB pins, sdkconfig for the CSI format), cross-checked against Espressif's
// esp-video-components SC2336 driver.
//
// Two things that look like omissions but are not:
//   - reset and power-down are both -1: this module exposes neither.
//   - there is no XCLK pin, because the host does not generate one. Espressif's
//     SC2336 driver defines SC2336_ENABLE_OUT_XCLK as an EMPTY macro and
//     esp_video's CSI config struct has no xclk field - the module self-clocks
//     at 24 MHz. Do not go looking for a clock GPIO to wire up.
//
// The sensor's native 1024x600 is pixel-identical to the panel, so a fullscreen
// viewfinder is a 1:1 blit.
struct CameraPins {
  int8_t sccbPort;           // I2C peripheral index (1 = Wire1)
  int8_t sccbScl;
  int8_t sccbSda;
  int8_t resetPin;           // -1 when the module has none
  int8_t pwdnPin;            // -1 when the module has none
  uint8_t sccbAddr;          // 7-bit SCCB address (SC2336: 0x30)
  uint8_t csiLanes;
  uint16_t laneBitRateMbps;  // per-lane MIPI-CSI bit rate
  uint16_t width;
  uint16_t height;
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
  CameraPins camera;
  const char *revisionNote;
};

const HardwareProfile &activeHardwareProfile();
const HardwareProfile &profileFor(int profileId);
void printHardwareProfile(Stream &out, const HardwareProfile &profile);

#endif
