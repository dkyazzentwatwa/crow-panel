#include "Pn532Reader.h"

#if USE_PN532_DRIVER
#include <Adafruit_PN532.h>
#endif

void Pn532Reader::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_PN532_DRIVER
  Logger::warn("pn532", "Adafruit_PN532 include enabled. Choose I2C, SPI, or UART mode from the verified module wiring.");
#else
  Logger::info("pn532", "driver disabled; PN532 is a compile-safe stub");
#endif
}

bool Pn532Reader::poll(BadgeRead &read) {
  (void)read;
  return false;
}

const char *Pn532Reader::driverName() const {
  return "pn532-stub";
}
