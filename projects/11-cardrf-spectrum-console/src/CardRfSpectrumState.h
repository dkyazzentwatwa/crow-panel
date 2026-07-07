#ifndef CARDRF_SPECTRUM_STATE_H
#define CARDRF_SPECTRUM_STATE_H

#include <Arduino.h>
#include "CardRfTypes.h"

class CardRfSpectrumState {
 public:
  void begin(const String &presetName);
  void setPreset(const String &presetName);
  void stop();
  void applyScanRow(const CardRfScanRow &row, const String &source);
  void applyPower(const CardRfPowerSample &sample, const String &source);

  const String &preset() const;
  bool scanning() const;
  uint16_t rowCount() const;
  uint16_t peakPower() const;
  String scanLabel() const;
  String rowsLabel() const;
  String peakLabel() const;
  String powerLabel() const;
  String heatmapLabel() const;
  String sourceLabel() const;
  String bridgeLabel(bool enabled) const;
  String scanDetail() const;
  String powerDetail() const;
  String statusDetail() const;

 private:
  String formatMhz(uint32_t hz) const;
  String heatmapBars() const;
  String binList() const;

  String preset_ = "433 ISM";
  String source_ = "mock";
  bool scanning_ = false;
  bool hasScanRow_ = false;
  bool hasPower_ = false;
  uint16_t rowCount_ = 0;
  uint16_t peakPower_ = 0;
  CardRfScanRow latestRow_;
  CardRfPowerSample latestPower_;
};

#endif
