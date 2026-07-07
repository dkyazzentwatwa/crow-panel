#ifndef NFC_FIELD_LAB_NDEF_PREVIEW_H
#define NFC_FIELD_LAB_NDEF_PREVIEW_H

#include <Arduino.h>

String nfcBytesToHex(const uint8_t *data, uint16_t length, uint8_t maxBytes);
String decodeNdefRecordPreview(const uint8_t *data, uint16_t length);

#endif
