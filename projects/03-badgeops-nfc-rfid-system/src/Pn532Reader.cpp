// PN532 path: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8) with
// Adafruit PN532 1.3.x. NOT HARDWARE-VERIFIED. Cross-check your module's
// mode switches (this scaffold assumes I2C) and wiring before power-on.
// I2C rides the touch bus (SDA=45, SCL=46): GT911 answers at 0x5D/0x14,
// the PN532 at 0x24 - no address conflict.

#include "Pn532Reader.h"

#if USE_PN532_DRIVER
#include <Wire.h>
#include <Adafruit_PN532.h>

// Set the IRQ/RESET wiring in config/Pins.h (copy from Pins.example.h).
// -1 means "not configured yet": the reader then refuses to start instead
// of driving a guessed pin.
#ifndef BADGEOPS_PN532_IRQ
#define BADGEOPS_PN532_IRQ -1
#endif
#ifndef BADGEOPS_PN532_RESET
#define BADGEOPS_PN532_RESET -1
#endif

namespace {
// File-static: one PN532 per sketch is plenty for this kiosk.
Adafruit_PN532 *nfc = nullptr;

String formatUid(const uint8_t *uid, uint8_t length) {
  String out;
  for (uint8_t i = 0; i < length; i++) {
    if (uid[i] < 0x10) {
      out += "0";
    }
    out += String(uid[i], HEX);
    if (i + 1 < length) {
      out += ":";
    }
  }
  out.toUpperCase();  // match the BadgeRegistry "AA:BB:CC:DD" format
  return out;
}
}  // namespace
#endif

void Pn532Reader::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_PN532_DRIVER
  if (BADGEOPS_PN532_IRQ < 0) {
    Logger::error("pn532", "pins not configured; copy config/Pins.example.h to Pins.h and set BADGEOPS_PN532_IRQ/RESET");
    return;
  }
  Wire.begin(profile.touch.sda, profile.touch.scl);
  nfc = new Adafruit_PN532(BADGEOPS_PN532_IRQ, BADGEOPS_PN532_RESET, &Wire);
  nfc->begin();
  uint32_t version = nfc->getFirmwareVersion();
  if (version == 0) {
    Logger::error("pn532", "no PN532 on I2C 0x24; check wiring and module mode switches");
    return;
  }
  nfc->SAMConfig();
  ready_ = true;
  Logger::info("pn532", "PN532 ready, firmware " + String((version >> 16) & 0xFF) + "." + String((version >> 8) & 0xFF));
#else
  Logger::info("pn532", "driver disabled; PN532 is a compile-safe stub");
#endif
}

bool Pn532Reader::poll(BadgeRead &read) {
#if USE_PN532_DRIVER
  if (!ready_ || !pollGate_.ready()) {
    return false;
  }
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  // 25 ms timeout is the closest the Adafruit API gets to non-blocking;
  // combined with the 250 ms poll gate the loop stays responsive.
  if (!nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 25)) {
    return false;
  }
  String formatted = formatUid(uid, uidLength);
  // Debounce: a card held on the reader reports continuously.
  if (formatted == lastUid_ && millis() - lastUidMs_ < 1500) {
    return false;
  }
  lastUid_ = formatted;
  lastUidMs_ = millis();
  read.uid = formatted;
  read.reader = "pn532";
  read.readAtMs = millis();
  Logger::info("pn532", "tap uid=" + formatted);
  return true;
#else
  (void)read;
  return false;
#endif
}

const char *Pn532Reader::driverName() const {
#if USE_PN532_DRIVER
  return "pn532-i2c";
#else
  return "pn532-stub";
#endif
}
