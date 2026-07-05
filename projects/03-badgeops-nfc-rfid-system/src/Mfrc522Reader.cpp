#include "Mfrc522Reader.h"

#if USE_MFRC522_DRIVER
#include <MFRC522.h>
#endif

void Mfrc522Reader::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_MFRC522_DRIVER
  Logger::warn("mfrc522", "MFRC522 include enabled. Confirm SPI SS/RST pins before initialization.");
#else
  Logger::info("mfrc522", "driver disabled; MFRC522 is a compile-safe stub");
#endif
}

bool Mfrc522Reader::poll(BadgeRead &read) {
  (void)read;
  return false;
}

const char *Mfrc522Reader::driverName() const {
  return "mfrc522-stub";
}
