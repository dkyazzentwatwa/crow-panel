#include "MockNfcReader.h"

namespace {
const char *const MOCK_UIDS[] = {
  "04:A1:22:9C",
  "7A:31:90:0D",
  "C2:44:10:AA"
};
}

void MockNfcReader::begin(const HardwareProfile &profile) {
  (void)profile;
  Logger::info("nfc-mock", "mock NFC reader ready; no PN532 or MFRC522 hardware required");
}

bool MockNfcReader::pollUid(NfcUidRead &read) {
  const char *uid = MOCK_UIDS[index_ % (sizeof(MOCK_UIDS) / sizeof(MOCK_UIDS[0]))];
  index_++;
  read.uid = uid;
  read.tagType = "NTAG213 mock";
  read.reader = driverName();
  read.capacityBytes = 144;
  read.readAtMs = millis();
  read.fromMock = true;
  return true;
}

bool MockNfcReader::readNdefPreview(NdefPreview &preview) {
  preview.source = driverName();
  preview.recordType = "Type 4 NDEF mock";
  preview.payload = "uri https://techtiff.ai/lab";
  preview.byteCount = 30;
  preview.fromMock = true;
  return true;
}

bool MockNfcReader::readType4Ndef(SafeApduRead &result) {
  result.source = driverName();
  result.trace = "SELECT NDEF AID 9000; SELECT NDEF file 9000; READ NLEN 001E";
  result.preview = "uri https://techtiff.ai/lab";
  result.ndefLength = 30;
  result.previewBytes = 30;
  result.fromMock = true;
  return true;
}

bool MockNfcReader::supportsNdefPreview() const {
  return true;
}

bool MockNfcReader::supportsType4Ndef() const {
  return true;
}

const char *MockNfcReader::driverName() const {
  return "mock-nfc";
}
