#ifndef NFC_FIELD_LAB_PN532_NFC_READER_H
#define NFC_FIELD_LAB_PN532_NFC_READER_H

#include "NfcReader.h"

class Pn532NfcReader : public NfcReader {
 public:
  void begin(const HardwareProfile &profile) override;
  bool pollUid(NfcUidRead &read) override;
  bool readNdefPreview(NdefPreview &preview) override;
  bool readType4Ndef(SafeApduRead &result) override;
  bool supportsNdefPreview() const override;
  bool supportsType4Ndef() const override;
  const char *driverName() const override;

 private:
  bool ready_ = false;
  Throttle pollGate_{250};
  String lastUid_;
  unsigned long lastUidMs_ = 0;
};

#endif
