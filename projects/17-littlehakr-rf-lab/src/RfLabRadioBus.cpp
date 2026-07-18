#include "RfLabRadioBus.h"
#include "../config/ProjectConfig.h"

namespace {
SPISettings kRfLabSpiSettings(RF_LAB_SPI_HZ, MSBFIRST, SPI_MODE0);
}

bool RfLabRadioBus::begin() {
  pinMode(RF_LAB_NRF_CSN, OUTPUT);
  pinMode(RF_LAB_NRF_CE, OUTPUT);
  pinMode(RF_LAB_CC1101_CS, OUTPUT);
  pinMode(RF_LAB_CC1101_GDO0, INPUT);
  pinMode(RF_LAB_CC1101_GDO2, INPUT);
  digitalWrite(RF_LAB_NRF_CSN, HIGH);
  digitalWrite(RF_LAB_NRF_CE, LOW);
  digitalWrite(RF_LAB_CC1101_CS, HIGH);
  spi_.begin(RF_LAB_SCK, RF_LAB_MISO, RF_LAB_MOSI);
  return true;
}

void RfLabRadioBus::select_(int pin) {
  spi_.beginTransaction(kRfLabSpiSettings);
  digitalWrite(pin, LOW);
}

void RfLabRadioBus::release_(int pin) {
  digitalWrite(pin, HIGH);
  spi_.endTransaction();
}

uint8_t RfLabRadioBus::nrfCommand(uint8_t command) {
  select_(RF_LAB_NRF_CSN);
  uint8_t result = spi_.transfer(command);
  release_(RF_LAB_NRF_CSN);
  return result;
}

uint8_t RfLabRadioBus::nrfRead(uint8_t reg) {
  select_(RF_LAB_NRF_CSN);
  spi_.transfer(reg & 0x1F);
  uint8_t value = spi_.transfer(0xFF);
  release_(RF_LAB_NRF_CSN);
  return value;
}

void RfLabRadioBus::nrfWrite(uint8_t reg, uint8_t value) {
  select_(RF_LAB_NRF_CSN);
  spi_.transfer((reg & 0x1F) | 0x20);
  spi_.transfer(value);
  release_(RF_LAB_NRF_CSN);
}

void RfLabRadioBus::nrfStrobe(uint8_t command) { nrfCommand(command); }

uint8_t RfLabRadioBus::ccReadStatus(uint8_t reg) {
  select_(RF_LAB_CC1101_CS);
  spi_.transfer((reg & 0x3F) | 0xC0);
  uint8_t value = spi_.transfer(0xFF);
  release_(RF_LAB_CC1101_CS);
  return value;
}

void RfLabRadioBus::ccWrite(uint8_t reg, uint8_t value) {
  select_(RF_LAB_CC1101_CS);
  spi_.transfer(reg & 0x3F);
  spi_.transfer(value);
  release_(RF_LAB_CC1101_CS);
}

void RfLabRadioBus::ccStrobe(uint8_t command) {
  select_(RF_LAB_CC1101_CS);
  spi_.transfer(command);
  release_(RF_LAB_CC1101_CS);
}
