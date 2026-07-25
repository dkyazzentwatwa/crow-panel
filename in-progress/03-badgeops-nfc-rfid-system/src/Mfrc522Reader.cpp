// MFRC522 path: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8) with
// MFRC522 1.4.x. NOT HARDWARE-VERIFIED. Default SS=10/RST=9 are the
// wireless-socket SPI pins - physically remove any socket module (SX1262,
// nRF24) before wiring an MFRC522 there, and never enable two transports
// on the same bus in one bring-up stage.

#include "Mfrc522Reader.h"

#if USE_MFRC522_DRIVER
#include <SPI.h>
#include <MFRC522.h>

// Override in config/Pins.h (copy from Pins.example.h) if you wire the
// module to different header pins.
#ifndef BADGEOPS_MFRC522_SS
#define BADGEOPS_MFRC522_SS 10
#endif
#ifndef BADGEOPS_MFRC522_RST
#define BADGEOPS_MFRC522_RST 9
#endif

namespace {
MFRC522 *rfid = nullptr;

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

void Mfrc522Reader::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_MFRC522_DRIVER
  SPI.begin(profile.wireless.spiClk, profile.wireless.spiMiso,
            profile.wireless.spiMosi, BADGEOPS_MFRC522_SS);
  rfid = new MFRC522(BADGEOPS_MFRC522_SS, BADGEOPS_MFRC522_RST);
  rfid->PCD_Init();
  byte version = rfid->PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Logger::error("mfrc522", "no MFRC522 on SPI (version reg 0x" + String(version, HEX) + "); check SS/RST wiring");
    return;
  }
  ready_ = true;
  Logger::info("mfrc522", "MFRC522 ready, version reg 0x" + String(version, HEX));
#else
  Logger::info("mfrc522", "driver disabled; MFRC522 is a compile-safe stub");
#endif
}

bool Mfrc522Reader::poll(BadgeRead &read) {
#if USE_MFRC522_DRIVER
  if (!ready_ || !pollGate_.ready()) {
    return false;
  }
  if (!rfid->PICC_IsNewCardPresent() || !rfid->PICC_ReadCardSerial()) {
    return false;
  }
  String formatted = formatUid(rfid->uid.uidByte, rfid->uid.size);
  rfid->PICC_HaltA();
  rfid->PCD_StopCrypto1();
  // Debounce: some cards re-announce while still on the antenna.
  if (formatted == lastUid_ && millis() - lastUidMs_ < 1500) {
    return false;
  }
  lastUid_ = formatted;
  lastUidMs_ = millis();
  read.uid = formatted;
  read.reader = "mfrc522";
  read.readAtMs = millis();
  Logger::info("mfrc522", "tap uid=" + formatted);
  return true;
#else
  (void)read;
  return false;
#endif
}

const char *Mfrc522Reader::driverName() const {
#if USE_MFRC522_DRIVER
  return "mfrc522-spi";
#else
  return "mfrc522-stub";
#endif
}
