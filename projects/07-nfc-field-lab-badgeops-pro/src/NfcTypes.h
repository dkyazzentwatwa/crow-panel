#ifndef NFC_FIELD_LAB_NFC_TYPES_H
#define NFC_FIELD_LAB_NFC_TYPES_H

#include <Arduino.h>

struct NfcUidRead {
  String uid;
  String tagType;
  String reader;
  uint16_t capacityBytes = 0;
  unsigned long readAtMs = 0;
  bool fromMock = false;
};

struct NdefPreview {
  String source;
  String recordType;
  String payload;
  uint16_t byteCount = 0;
  bool fromMock = false;
};

struct SafeApduRead {
  String source;
  String trace;
  String preview;
  uint16_t ndefLength = 0;
  uint16_t previewBytes = 0;
  bool fromMock = false;
};

#endif
