#include "Mfrc522NfcReader.h"

#if USE_MFRC522_DRIVER
#include <MFRC522.h>
#include <SPI.h>

namespace {
MFRC522 *mfrc522 = nullptr;

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
  out.toUpperCase();
  return out;
}
}
#endif

void Mfrc522NfcReader::begin(const HardwareProfile &profile) {
#if USE_MFRC522_DRIVER
  SPI.begin(profile.wireless.spiClk, profile.wireless.spiMiso,
            profile.wireless.spiMosi, NFC_LAB_MFRC522_SS);
  mfrc522 = new MFRC522(NFC_LAB_MFRC522_SS, NFC_LAB_MFRC522_RST);
  mfrc522->PCD_Init();

  const byte version = mfrc522->PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Logger::error("mfrc522", "no MFRC522 on SPI; check SS/RST wiring");
    return;
  }

  ready_ = true;
  Logger::info("mfrc522", "MFRC522 ready version 0x" + String(version, HEX));
#else
  (void)profile;
  Logger::info("mfrc522", "driver disabled");
#endif
}

bool Mfrc522NfcReader::pollUid(NfcUidRead &read) {
#if USE_MFRC522_DRIVER
  if (!ready_ || !pollGate_.ready()) {
    return false;
  }
  if (!mfrc522->PICC_IsNewCardPresent() || !mfrc522->PICC_ReadCardSerial()) {
    return false;
  }

  const String formatted = formatUid(mfrc522->uid.uidByte, mfrc522->uid.size);
  mfrc522->PICC_HaltA();
  mfrc522->PCD_StopCrypto1();
  if (formatted == lastUid_ && millis() - lastUidMs_ < 1500) {
    return false;
  }

  lastUid_ = formatted;
  lastUidMs_ = millis();
  read.uid = formatted;
  read.tagType = "MIFARE/ISO14443A UID";
  read.reader = driverName();
  read.capacityBytes = 0;
  read.readAtMs = millis();
  read.fromMock = false;
  Logger::info("mfrc522", "uid read " + formatted);
  return true;
#else
  (void)read;
  return false;
#endif
}

bool Mfrc522NfcReader::readNdefPreview(NdefPreview &preview) {
  (void)preview;
  return false;
}

bool Mfrc522NfcReader::readType4Ndef(SafeApduRead &result) {
  (void)result;
  return false;
}

bool Mfrc522NfcReader::supportsNdefPreview() const {
  return false;
}

bool Mfrc522NfcReader::supportsType4Ndef() const {
  return false;
}

const char *Mfrc522NfcReader::driverName() const {
#if USE_MFRC522_DRIVER
  return "mfrc522-spi";
#else
  return "mfrc522-disabled";
#endif
}
