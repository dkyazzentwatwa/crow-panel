#ifndef LITTLEHAKR_RF_LAB_TYPES_H
#define LITTLEHAKR_RF_LAB_TYPES_H

#include <Arduino.h>

enum RfLabProofState : uint8_t {
  kRfLabSourceReview = 0,
  kRfLabSpiReady,
  kRfLabNrfDetected,
  kRfLabCcDetected,
  kRfLabBothDetected,
  kRfLabError
};

struct RfLabState {
  bool spiReady = false;
  bool nrfDetected = false;
  bool ccDetected = false;
  bool detectorAuthorized = false;
  bool detectorRunning = false;
  bool gdo0High = false;
  bool gdo2High = false;
  uint8_t nrfStatus = 0xFF;
  uint8_t ccPartnum = 0xFF;
  uint8_t ccVersion = 0xFF;
  uint8_t nrfRpd = 0;
  int16_t ccRssiDbm = -127;
  int16_t ccMinRssiDbm = 127;
  int16_t ccMaxRssiDbm = -127;
  uint32_t nrfSamples = 0;
  uint32_t nrfActivityHits = 0;
  uint32_t ccSamples = 0;
  uint32_t ccActivityHits = 0;
  uint32_t gdoTransitions = 0;
  uint32_t sessionStartedMs = 0;
  RfLabProofState proof = kRfLabSourceReview;
};

inline const char *rfLabProofLabel(RfLabProofState proof) {
  switch (proof) {
    case kRfLabSpiReady: return "SPI_READY";
    case kRfLabNrfDetected: return "NRF24_DETECTED";
    case kRfLabCcDetected: return "CC1101_DETECTED";
    case kRfLabBothDetected: return "BOTH_RADIOS_DETECTED";
    case kRfLabError: return "ERROR_STATE";
    default: return "SOURCE_REVIEW";
  }
}

#endif
