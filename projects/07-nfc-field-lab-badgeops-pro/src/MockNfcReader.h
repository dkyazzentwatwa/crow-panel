#ifndef NFC_FIELD_LAB_MOCK_NFC_READER_H
#define NFC_FIELD_LAB_MOCK_NFC_READER_H

#include "NfcReader.h"

class MockNfcReader : public NfcReader {
 public:
  void begin(const HardwareProfile &profile) override;
  bool pollUid(NfcUidRead &read) override;
  bool readNdefPreview(NdefPreview &preview) override;
  bool readType4Ndef(SafeApduRead &result) override;
  bool supportsNdefPreview() const override;
  bool supportsType4Ndef() const override;
  const char *driverName() const override;

 private:
  uint8_t index_ = 0;
};

#endif
