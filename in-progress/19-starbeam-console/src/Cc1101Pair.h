#ifndef STARBEAM_CONSOLE_CC1101_PAIR_H
#define STARBEAM_CONSOLE_CC1101_PAIR_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"
#include "RadioBus.h"

// Two CC1101 radios on the shared SPI bus. Ports project-starbeam's cc1101.cpp
// (init, RSSI frequency scan, OOK jam via SendData, RSSI/LQI). The SmartRC
// driver is a singleton, so each op re-points it at the target chip's CS/GDO0
// before running (both chips share SCK/MOSI/MISO). GDO2 is left unconnected.
// Compiled only when USE_STARBEAM_RADIOS=1.

class Cc1101Pair {
 public:
  explicit Cc1101Pair(RadioBus &bus) : bus_(bus) {}

  void begin();
  bool present(uint8_t idx) const { return present_[idx & 1]; }

  void setFrequency(float mhz);        // both chips
  void jam(uint8_t idx, bool armed);   // idx 0/1, or 2 for both
  float rssi(uint8_t idx);
  int lqi(uint8_t idx);
  void reset();

  // Frequency scan: step the sweep cursor between lo/hi, returning the current
  // frequency and its RSSI. Wraps at hi. Fills the panel's found-signal view.
  bool sweepStep(float lo, float hi, float &freqOut, float &rssiOut);

  float freqMhz() const { return freqMhz_; }

 private:
#if USE_STARBEAM_RADIOS
  void select_(uint8_t idx);           // re-point the singleton at chip idx
  void applyProfile_();
  float sweepCursor_ = 0.0f;
#endif
  RadioBus &bus_;
  bool present_[2] = {false, false};
  float freqMhz_ = 433.92f;
};

#endif  // STARBEAM_CONSOLE_CC1101_PAIR_H
