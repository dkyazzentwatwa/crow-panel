#ifndef STARBEAM_CONSOLE_RADIO_BUS_H
#define STARBEAM_CONSOLE_RADIO_BUS_H

#include <Arduino.h>
#include <SPI.h>

// One SPI bus shared by all five nRF24L01+ and both CC1101 radios (authentic
// to Starbeam, which shared HSPI across radios). Each radio has a unique CS;
// nRF24 additionally has a unique CE. This class owns the SPIClass and provides
// library-free register probing so the panel can prove every radio is present
// even in a build without the RF24 / SmartRC-CC1101 libraries.

class RadioBus {
 public:
  bool begin();

  // Raw register proof (no external library needed).
  // nRF: reads STATUS (0x07). Present when the byte is neither floating-high
  // (0xFF) nor stuck-low (0x00).
  bool probeNrf(uint8_t index, uint8_t &status);
  // CC1101: reads VERSION (0x31) + PARTNUM (0x30) status registers. Present
  // when VERSION is plausible (typically 0x14/0x04, never 0x00/0xFF).
  bool probeCc(uint8_t index, uint8_t &partnum, uint8_t &version);

  int nrfCsn(uint8_t index) const { return nrfCsn_[index]; }
  int nrfCe(uint8_t index) const { return nrfCe_[index]; }
  int ccCs(uint8_t index) const { return ccCs_[index]; }

  SPIClass &spi() { return SPI; }

 private:
  void select_(int cs);
  void release_(int cs);

  int nrfCsn_[5];
  int nrfCe_[5];
  int ccCs_[2];
  int ccGdo0_[2];
  bool begun_ = false;
};

#endif  // STARBEAM_CONSOLE_RADIO_BUS_H
