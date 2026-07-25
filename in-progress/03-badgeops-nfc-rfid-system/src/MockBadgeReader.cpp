#include "MockBadgeReader.h"

void MockBadgeReader::begin(const HardwareProfile &profile) {
  profile_ = &profile;
  Logger::info("badge-reader", "mock badge reader ready; no PN532 or MFRC522 hardware required");
}

bool MockBadgeReader::poll(BadgeRead &read) {
  if (!cadence_.ready()) {
    return false;
  }

  read.uid = MockData::badgeUid(index_);
  read.reader = "mock";
  read.readAtMs = millis();
  index_ = (index_ + 1) % 5;
  Logger::info("badge-reader", "mock tap uid=" + read.uid);
  return true;
}

const char *MockBadgeReader::driverName() const {
  return "mock-badge-reader";
}
