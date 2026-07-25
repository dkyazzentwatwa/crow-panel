#include "RadioBus.h"
#include "../config/ProjectConfig.h"

namespace {
SPISettings kBusSettings(STARBEAM_NRF_SPI_HZ, MSBFIRST, SPI_MODE0);
}

bool RadioBus::begin() {
  nrfCsn_[0] = STARBEAM_NRF0_CSN; nrfCsn_[1] = STARBEAM_NRF1_CSN;
  nrfCsn_[2] = STARBEAM_NRF2_CSN; nrfCsn_[3] = STARBEAM_NRF3_CSN;
  nrfCsn_[4] = STARBEAM_NRF4_CSN;
  nrfCe_[0] = STARBEAM_NRF0_CE; nrfCe_[1] = STARBEAM_NRF1_CE;
  nrfCe_[2] = STARBEAM_NRF2_CE; nrfCe_[3] = STARBEAM_NRF3_CE;
  nrfCe_[4] = STARBEAM_NRF4_CE;
  ccCs_[0] = STARBEAM_CC0_CS; ccCs_[1] = STARBEAM_CC1_CS;
  ccGdo0_[0] = STARBEAM_CC0_GDO0; ccGdo0_[1] = STARBEAM_CC1_GDO0;

  for (uint8_t i = 0; i < 5; ++i) {
    pinMode(nrfCsn_[i], OUTPUT);
    pinMode(nrfCe_[i], OUTPUT);
    digitalWrite(nrfCsn_[i], HIGH);
    digitalWrite(nrfCe_[i], LOW);
  }
  for (uint8_t i = 0; i < 2; ++i) {
    pinMode(ccCs_[i], OUTPUT);
    pinMode(ccGdo0_[i], INPUT);
    digitalWrite(ccCs_[i], HIGH);
  }

  SPI.begin(STARBEAM_SPI_SCK, STARBEAM_SPI_MISO, STARBEAM_SPI_MOSI);
  begun_ = true;
  return true;
}

void RadioBus::select_(int cs) {
  SPI.beginTransaction(kBusSettings);
  digitalWrite(cs, LOW);
}

void RadioBus::release_(int cs) {
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

bool RadioBus::probeNrf(uint8_t index, uint8_t &status) {
  if (!begun_ || index >= 5) return false;
  select_(nrfCsn_[index]);
  status = SPI.transfer(0x07 & 0x1F);  // R_REGISTER | STATUS; STATUS returns on cmd byte
  release_(nrfCsn_[index]);
  return status != 0xFF && status != 0x00;
}

bool RadioBus::probeCc(uint8_t index, uint8_t &partnum, uint8_t &version) {
  if (!begun_ || index >= 2) return false;
  int cs = ccCs_[index];
  select_(cs);
  SPI.transfer(0x30 | 0xC0);            // PARTNUM (burst-read status reg)
  partnum = SPI.transfer(0xFF);
  release_(cs);
  select_(cs);
  SPI.transfer(0x31 | 0xC0);            // VERSION
  version = SPI.transfer(0xFF);
  release_(cs);
  return version != 0xFF && version != 0x00;
}
