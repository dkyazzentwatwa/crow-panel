#ifndef NFC_FIELD_LAB_NFC_READER_H
#define NFC_FIELD_LAB_NFC_READER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>
#include "NfcTypes.h"

class NfcReader {
 public:
  virtual ~NfcReader() {}
  virtual void begin(const HardwareProfile &profile) = 0;
  virtual bool pollUid(NfcUidRead &read) = 0;
  virtual bool readNdefPreview(NdefPreview &preview) = 0;
  virtual bool readType4Ndef(SafeApduRead &result) = 0;
  virtual bool supportsNdefPreview() const = 0;
  virtual bool supportsType4Ndef() const = 0;
  virtual const char *driverName() const = 0;
};

#endif
