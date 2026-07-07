#include "CardRfSpectrumState.h"

void CardRfSpectrumState::begin(const String &presetName) {
  setPreset(presetName);
  source_ = "mock";
  scanning_ = false;
  hasScanRow_ = false;
  hasPower_ = false;
  rowCount_ = 0;
  peakPower_ = 0;
}

void CardRfSpectrumState::setPreset(const String &presetName) {
  preset_ = presetName;
  preset_.trim();
  if (preset_.length() == 0) {
    preset_ = "433 ISM";
  }
}

void CardRfSpectrumState::stop() {
  scanning_ = false;
}

void CardRfSpectrumState::applyScanRow(const CardRfScanRow &row, const String &source) {
  latestRow_ = row;
  source_ = source;
  scanning_ = true;
  hasScanRow_ = true;
  if (rowCount_ < 65535) {
    rowCount_++;
  }
  peakPower_ = row.maxPower;
}

void CardRfSpectrumState::applyPower(const CardRfPowerSample &sample, const String &source) {
  latestPower_ = sample;
  source_ = source;
  hasPower_ = true;
  if (!hasScanRow_) {
    peakPower_ = sample.raw;
  }
}

const String &CardRfSpectrumState::preset() const {
  return preset_;
}

bool CardRfSpectrumState::scanning() const {
  return scanning_;
}

uint16_t CardRfSpectrumState::rowCount() const {
  return rowCount_;
}

uint16_t CardRfSpectrumState::peakPower() const {
  return peakPower_;
}

String CardRfSpectrumState::scanLabel() const {
  return scanning_ ? "RUN" : "STOP";
}

String CardRfSpectrumState::rowsLabel() const {
  return String(rowCount_);
}

String CardRfSpectrumState::peakLabel() const {
  return hasScanRow_ || hasPower_ ? String(peakPower_) : "--";
}

String CardRfSpectrumState::powerLabel() const {
  if (!hasPower_) {
    return "--";
  }
  return String("RAW ") + String(latestPower_.raw);
}

String CardRfSpectrumState::heatmapLabel() const {
  if (!hasScanRow_) {
    return "waiting";
  }
  return heatmapBars();
}

String CardRfSpectrumState::sourceLabel() const {
  return source_;
}

String CardRfSpectrumState::bridgeLabel(bool enabled) const {
  return enabled ? "UART RX" : "mock";
}

String CardRfSpectrumState::scanDetail() const {
  if (!hasScanRow_) {
    return "No SCANROW parsed yet|Use scan or feed SCANROW ...|RX-only state";
  }
  return String("Start ") + formatMhz(latestRow_.startHz) + "|Step " +
         formatMhz(latestRow_.stepHz) + "|Bins " + String(latestRow_.binCount) +
         " Min " + String(latestRow_.minPower) + " Max " + String(latestRow_.maxPower) +
         "|Map " + heatmapBars() + "|Data " + binList();
}

String CardRfSpectrumState::powerDetail() const {
  if (!hasPower_) {
    return "No POWER parsed yet|Use power or feed POWER ...|Uncalibrated RX estimate";
  }
  return String("RAW ") + String(latestPower_.raw) + "|CLIP " +
         String(latestPower_.clipped ? 1 : 0) + "|SAMPLES " +
         String(latestPower_.samples) + "|Source " + source_;
}

String CardRfSpectrumState::statusDetail() const {
  return String("Preset ") + preset_ + "|Rows " + String(rowCount_) +
         "|Peak " + peakLabel() + "|Source " + source_ +
         "|Safety RX only";
}

String CardRfSpectrumState::formatMhz(uint32_t hz) const {
  uint32_t whole = hz / 1000000UL;
  uint32_t frac = (hz % 1000000UL) / 1000UL;
  String formatted = String(whole) + ".";
  if (frac < 100) {
    formatted += "0";
  }
  if (frac < 10) {
    formatted += "0";
  }
  formatted += String(frac);
  formatted += " MHz";
  return formatted;
}

String CardRfSpectrumState::heatmapBars() const {
  if (!hasScanRow_) {
    return "waiting";
  }
  const char *levels = ".:-=+*#%@";
  const uint8_t levelCount = 9;
  uint16_t span = latestRow_.maxPower > latestRow_.minPower
                      ? latestRow_.maxPower - latestRow_.minPower
                      : 1;
  String bars;
  for (uint8_t i = 0; i < latestRow_.binCount; i++) {
    uint16_t value = latestRow_.bins[i];
    uint16_t normalized = value > latestRow_.minPower ? value - latestRow_.minPower : 0;
    uint8_t index = (uint8_t)((normalized * (levelCount - 1)) / span);
    if (index >= levelCount) {
      index = levelCount - 1;
    }
    bars += levels[index];
  }
  return bars;
}

String CardRfSpectrumState::binList() const {
  String list;
  for (uint8_t i = 0; i < latestRow_.binCount; i++) {
    if (i > 0) {
      list += ",";
    }
    list += String(latestRow_.bins[i]);
  }
  return list;
}
