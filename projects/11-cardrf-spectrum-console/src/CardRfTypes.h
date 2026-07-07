#ifndef CARDRF_TYPES_H
#define CARDRF_TYPES_H

#include <Arduino.h>

static const uint8_t kCardRfMaxBins = 32;

enum CardRfLineKind {
  CARD_RF_LINE_NONE = 0,
  CARD_RF_LINE_SCANROW,
  CARD_RF_LINE_POWER
};

struct CardRfScanRow {
  uint32_t startHz = 0;
  uint32_t stepHz = 0;
  uint8_t binCount = 0;
  uint16_t minPower = 0;
  uint16_t maxPower = 0;
  uint8_t bins[kCardRfMaxBins] = {0};
};

struct CardRfPowerSample {
  uint16_t raw = 0;
  bool clipped = false;
  uint16_t samples = 0;
};

struct CardRfLine {
  CardRfLineKind kind = CARD_RF_LINE_NONE;
  CardRfScanRow scanRow;
  CardRfPowerSample power;
};

#endif
