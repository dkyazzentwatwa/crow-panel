#ifndef LITTLEHAKR_RF_LAB_RADIO_BUS_H
#define LITTLEHAKR_RF_LAB_RADIO_BUS_H

#include <Arduino.h>
#include <SPI.h>

class RfLabRadioBus {
 public:
  explicit RfLabRadioBus(SPIClass &spi) : spi_(spi) {}

  bool begin();
  uint8_t nrfCommand(uint8_t command);
  uint8_t nrfRead(uint8_t reg);
  void nrfWrite(uint8_t reg, uint8_t value);
  void nrfStrobe(uint8_t command);
  uint8_t ccReadStatus(uint8_t reg);
  void ccWrite(uint8_t reg, uint8_t value);
  void ccStrobe(uint8_t command);

 private:
  void select_(int pin);
  void release_(int pin);

  SPIClass &spi_;
};

#endif
