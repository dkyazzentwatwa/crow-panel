#ifndef STARBEAM_CONSOLE_NRF24_ARRAY_H
#define STARBEAM_CONSOLE_NRF24_ARRAY_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"
#include "RadioBus.h"
#include "StarbeamTypes.h"

// Five nRF24L01+ radios on the shared SPI bus. Ports project-starbeam's
// nrf24.cpp (jammers via startConstCarrier + per-radio setChannel) and
// analyzer.cpp (2.4 GHz spectrum via the RPD register). Compiled only when
// USE_STARBEAM_RADIOS=1 (needs the RF24 library); otherwise the methods are
// no-op stubs so the UI and the rest of the panel still build.

class Nrf24Array {
 public:
  explicit Nrf24Array(RadioBus &bus) : bus_(bus) {}

  void begin();                       // init all five radios (const-carrier ready)
  bool radioPresent(uint8_t i) const { return present_[i]; }

  // Jammers — hop all radios each call. armed=false makes them refuse to TX.
  void btJam(bool armed);             // channels 0-81
  void droneJam(bool armed);          // channels 0-126
  void wifiJam(bool armed);           // channels 1/6/14
  void singleChannel(bool armed);     // mixed ranges

  // Spectrum: sample one channel's RPD activity into `out[0..127]` (0..10).
  // Call repeatedly; advances an internal channel cursor. Returns peak.
  uint8_t sampleSpectrum(uint8_t out[128]);

  void stopAll();                     // drop CE on every radio

 private:
  RadioBus &bus_;
  bool present_[5] = {false, false, false, false, false};
#if USE_STARBEAM_RADIOS
  uint8_t history_[128] = {0};
  uint8_t cursor_ = 0;
#endif
};

#endif  // STARBEAM_CONSOLE_NRF24_ARRAY_H
