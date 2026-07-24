#ifndef STARBEAM_CONSOLE_RECORDING_H
#define STARBEAM_CONSOLE_RECORDING_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"

// 433 MHz raw signal capture/replay, ported from project-starbeam's
// recording.cpp. Captures the CC1101 #1 GDO0 line into a byte buffer and can
// replay it. Persistence moves from ESP32 EEPROM emulation to NVS/Preferences.
// The GDO toggling is gated behind USE_STARBEAM_RADIOS; the buffer, hex/bit
// views, and persistence build in every configuration.

class Recording {
 public:
  static constexpr uint16_t kBufferSize = 512;  // matches Starbeam EEPROM raw region

  void begin();

  // Single-shot raw capture of `count` GDO0 samples spaced `intervalUs` apart.
  void recordRaw(int intervalUs, uint16_t count);
  // Replay the buffer by driving GDO0. `armed` gates the transmit.
  void playRaw(int intervalUs, bool armed);

  void flush();
  bool save();     // to NVS
  bool load();     // from NVS

  uint16_t byteCount() const { return count_; }
  bool valid() const { return count_ > 0; }
  const uint8_t *buffer() const { return buf_; }

 private:
  uint8_t buf_[kBufferSize] = {0};
  uint16_t count_ = 0;
};

#endif  // STARBEAM_CONSOLE_RECORDING_H
