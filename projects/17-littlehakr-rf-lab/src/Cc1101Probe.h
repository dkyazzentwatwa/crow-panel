#ifndef LITTLEHAKR_RF_LAB_CC1101_PROBE_H
#define LITTLEHAKR_RF_LAB_CC1101_PROBE_H

#include <Arduino.h>
#include "RfLabRadioBus.h"

class Cc1101Probe {
 public:
  explicit Cc1101Probe(RfLabRadioBus &bus) : bus_(bus) {}

  bool detect(uint8_t &partnum, uint8_t &version);
  bool startReceiveOnly();
  bool sampleActivity(int16_t &rssiDbm, bool &gdo0High, bool &gdo2High);
  void stop();

 private:
  void setFrequency_();
  void flushRx_();

  RfLabRadioBus &bus_;
  bool configured_ = false;
};

#endif
