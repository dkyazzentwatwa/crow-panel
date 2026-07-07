#include "Pn532NfcReader.h"
#include "NdefPreview.h"

#if USE_PN532_DRIVER
#include <Adafruit_PN532.h>
#include <Wire.h>
#include <string.h>

namespace {
Adafruit_PN532 *pn532 = nullptr;

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

bool sendSafeReadApdu(const uint8_t *command, uint8_t commandLength,
                      uint8_t *response, uint8_t &responseLength) {
  if (!pn532 || commandLength > 20) {
    return false;
  }
  uint8_t mutableCommand[20] = {0};
  memcpy(mutableCommand, command, commandLength);
  uint8_t available = responseLength;
  const bool exchanged = pn532->inDataExchange(mutableCommand, commandLength,
                                               response, &available);
  responseLength = available;
  return exchanged &&
         responseLength >= 2 &&
         response[responseLength - 2] == 0x90 &&
         response[responseLength - 1] == 0x00;
}

bool extractNdefFile(const uint8_t *cc, uint8_t ccLength, uint16_t &fileId,
                     uint16_t &maxSize, uint8_t &readAccess) {
  fileId = 0xE104;
  maxSize = 0;
  readAccess = 0xFF;
  uint8_t index = 7;
  while (index + 1 < ccLength) {
    const uint8_t tag = cc[index];
    const uint8_t length = cc[index + 1];
    if (length == 0 || index + 2 + length > ccLength) {
      break;
    }
    if (tag == 0x04 && length >= 6) {
      fileId = (static_cast<uint16_t>(cc[index + 2]) << 8) | cc[index + 3];
      maxSize = (static_cast<uint16_t>(cc[index + 4]) << 8) | cc[index + 5];
      readAccess = cc[index + 6];
      return true;
    }
    index += 2 + length;
  }
  return false;
}

bool readBinary(uint16_t offset, uint8_t length, uint8_t *response,
                uint8_t &responseLength) {
  uint8_t command[] = {
    0x00,
    0xB0,
    static_cast<uint8_t>((offset >> 8) & 0xFF),
    static_cast<uint8_t>(offset & 0xFF),
    length
  };
  return sendSafeReadApdu(command, sizeof(command), response, responseLength);
}
}
#endif

void Pn532NfcReader::begin(const HardwareProfile &profile) {
#if USE_PN532_DRIVER
  if (NFC_LAB_PN532_IRQ < 0 || NFC_LAB_PN532_RESET < 0) {
    Logger::error("pn532", "PN532 pins are not configured; set NFC_LAB_PN532_IRQ and NFC_LAB_PN532_RESET");
    return;
  }

  Wire.begin(profile.touch.sda, profile.touch.scl);
  pn532 = new Adafruit_PN532(static_cast<uint8_t>(NFC_LAB_PN532_IRQ),
                             static_cast<uint8_t>(NFC_LAB_PN532_RESET),
                             &Wire);
  if (!pn532->begin()) {
    Logger::error("pn532", "PN532 begin failed");
    return;
  }

  const uint32_t version = pn532->getFirmwareVersion();
  if (version == 0) {
    Logger::error("pn532", "no PN532 found on I2C 0x24; check wiring and mode switches");
    return;
  }

  pn532->SAMConfig();
  ready_ = true;
  Logger::info("pn532", "PN532 ready firmware " +
                         String((version >> 16) & 0xFF) + "." +
                         String((version >> 8) & 0xFF));
#else
  (void)profile;
  Logger::info("pn532", "driver disabled");
#endif
}

bool Pn532NfcReader::pollUid(NfcUidRead &read) {
#if USE_PN532_DRIVER
  if (!ready_ || !pollGate_.ready()) {
    return false;
  }

  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  if (!pn532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 25)) {
    return false;
  }

  const String formatted = formatUid(uid, uidLength);
  if (formatted == lastUid_ && millis() - lastUidMs_ < 1500) {
    return false;
  }

  lastUid_ = formatted;
  lastUidMs_ = millis();
  read.uid = formatted;
  read.tagType = "ISO14443A";
  read.reader = driverName();
  read.capacityBytes = 0;
  read.readAtMs = millis();
  read.fromMock = false;
  Logger::info("pn532", "uid read " + formatted);
  return true;
#else
  (void)read;
  return false;
#endif
}

bool Pn532NfcReader::readNdefPreview(NdefPreview &preview) {
  SafeApduRead result;
  if (!readType4Ndef(result)) {
    return false;
  }
  preview.source = result.source;
  preview.recordType = "Type 4 NDEF";
  preview.payload = result.preview;
  preview.byteCount = result.previewBytes;
  preview.fromMock = false;
  return true;
}

bool Pn532NfcReader::readType4Ndef(SafeApduRead &result) {
#if USE_PN532_DRIVER
  result.source = driverName();
  result.fromMock = false;

  if (!ready_) {
    result.trace = "PN532 not ready";
    return false;
  }

  if (!pn532->inListPassiveTarget()) {
    result.trace = "no ISO14443A Type 4 target listed";
    return false;
  }

  uint8_t response[NFC_LAB_APDU_RESPONSE_BYTES] = {0};
  uint8_t responseLength = sizeof(response);

  const uint8_t selectNdefAid[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01,
    0x00
  };
  if (!sendSafeReadApdu(selectNdefAid, sizeof(selectNdefAid), response, responseLength)) {
    result.trace = "SELECT NDEF AID failed";
    return false;
  }
  String trace = "SELECT NDEF AID 9000";

  const uint8_t selectCcFile[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, 0xE1, 0x03};
  responseLength = sizeof(response);
  if (!sendSafeReadApdu(selectCcFile, sizeof(selectCcFile), response, responseLength)) {
    result.trace = trace + "; SELECT CC failed";
    return false;
  }
  trace += "; SELECT CC 9000";

  responseLength = sizeof(response);
  if (!readBinary(0, 15, response, responseLength) || responseLength < 17) {
    result.trace = trace + "; READ CC failed";
    return false;
  }

  uint16_t ndefFileId = 0xE104;
  uint16_t maxSize = 0;
  uint8_t readAccess = 0xFF;
  if (!extractNdefFile(response, responseLength - 2, ndefFileId, maxSize, readAccess)) {
    result.trace = trace + "; no NDEF file control TLV";
    return false;
  }
  if (readAccess != 0x00) {
    result.trace = trace + "; NDEF file is not public-read";
    return false;
  }
  trace += "; READ CC 9000";

  const uint8_t selectNdefFile[] = {
    0x00,
    0xA4,
    0x00,
    0x0C,
    0x02,
    static_cast<uint8_t>((ndefFileId >> 8) & 0xFF),
    static_cast<uint8_t>(ndefFileId & 0xFF)
  };
  responseLength = sizeof(response);
  if (!sendSafeReadApdu(selectNdefFile, sizeof(selectNdefFile), response, responseLength)) {
    result.trace = trace + "; SELECT NDEF file failed";
    return false;
  }
  trace += "; SELECT NDEF file 9000";

  responseLength = sizeof(response);
  if (!readBinary(0, 2, response, responseLength) || responseLength < 4) {
    result.trace = trace + "; READ NLEN failed";
    return false;
  }

  const uint16_t ndefLength = (static_cast<uint16_t>(response[0]) << 8) | response[1];
  result.ndefLength = ndefLength;
  trace += "; READ NLEN " + String(ndefLength);
  if (ndefLength == 0) {
    result.preview = "empty NDEF message";
    result.previewBytes = 0;
    result.trace = trace;
    return true;
  }
  if (maxSize > 0 && ndefLength > maxSize) {
    result.trace = trace + "; NDEF length exceeds CC max";
    return false;
  }

  const uint8_t previewBytes = ndefLength < NFC_LAB_MAX_NDEF_PREVIEW_BYTES ?
                               ndefLength : NFC_LAB_MAX_NDEF_PREVIEW_BYTES;
  responseLength = sizeof(response);
  if (!readBinary(2, previewBytes, response, responseLength) || responseLength < 2) {
    result.trace = trace + "; READ NDEF failed";
    return false;
  }

  const uint8_t messageBytes = responseLength - 2;
  result.preview = decodeNdefRecordPreview(response, messageBytes);
  result.previewBytes = messageBytes;
  result.trace = trace + "; READ NDEF " + String(messageBytes) +
                 "/" + String(ndefLength) + " bytes";
  Logger::info("pn532", "safe Type 4 NDEF preview read " + String(messageBytes) + " bytes");
  return true;
#else
  (void)result;
  return false;
#endif
}

bool Pn532NfcReader::supportsNdefPreview() const {
  return true;
}

bool Pn532NfcReader::supportsType4Ndef() const {
  return true;
}

const char *Pn532NfcReader::driverName() const {
#if USE_PN532_DRIVER
  return "pn532-i2c";
#else
  return "pn532-disabled";
#endif
}
