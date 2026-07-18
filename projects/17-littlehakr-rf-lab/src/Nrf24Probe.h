#ifndef LITTLEHAKR_RF_LAB_NRF24_PROBE_H
#define LITTLEHAKR_RF_LAB_NRF24_PROBE_H

#include <Arduino.h>
#include "RfLabRadioBus.h"

class Nrf24Probe {
 public:
  explicit Nrf24Probe(RfLabRadioBus &bus) : bus_(bus) {}

  bool detect(uint8_t &status);
  bool startReceiveOnly();
  bool sampleActivity(uint8_t &rpd);
  void stop();

 private:
  void flushRx_();

  RfLabRadioBus &bus_;
  bool configured_ = false;
};

#endif
