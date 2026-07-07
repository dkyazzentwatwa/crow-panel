#include "NdefPreview.h"

namespace {
String asciiFromBytes(const uint8_t *data, uint16_t length, uint16_t maxBytes) {
  String out;
  const uint16_t limit = length < maxBytes ? length : maxBytes;
  for (uint16_t i = 0; i < limit; i++) {
    char c = static_cast<char>(data[i]);
    out += (c >= 32 && c <= 126) ? c : '.';
  }
  if (length > limit) {
    out += "...";
  }
  return out;
}

String uriPrefix(uint8_t code) {
  switch (code) {
    case 0x01:
      return "http://www.";
    case 0x02:
      return "https://www.";
    case 0x03:
      return "http://";
    case 0x04:
      return "https://";
    case 0x05:
      return "tel:";
    case 0x06:
      return "mailto:";
    default:
      return "";
  }
}
}

String nfcBytesToHex(const uint8_t *data, uint16_t length, uint8_t maxBytes) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String out;
  const uint16_t limit = length < maxBytes ? length : maxBytes;
  for (uint16_t i = 0; i < limit; i++) {
    if (i > 0) {
      out += " ";
    }
    out += HEX_DIGITS[(data[i] >> 4) & 0x0F];
    out += HEX_DIGITS[data[i] & 0x0F];
  }
  if (length > limit) {
    out += " ...";
  }
  return out;
}

String decodeNdefRecordPreview(const uint8_t *data, uint16_t length) {
  if (length == 0) {
    return "empty NDEF message";
  }
  if (length < 3) {
    return "short NDEF payload hex=" + nfcBytesToHex(data, length, 24);
  }

  const uint8_t flags = data[0];
  const bool shortRecord = (flags & 0x10) != 0;
  const bool hasIdLength = (flags & 0x08) != 0;
  const uint8_t tnf = flags & 0x07;
  const uint8_t typeLength = data[1];
  uint16_t index = 2;
  uint32_t payloadLength = 0;

  if (shortRecord) {
    payloadLength = data[index++];
  } else {
    if (length < 6) {
      return "truncated long NDEF header";
    }
    payloadLength = (static_cast<uint32_t>(data[index]) << 24) |
                    (static_cast<uint32_t>(data[index + 1]) << 16) |
                    (static_cast<uint32_t>(data[index + 2]) << 8) |
                    data[index + 3];
    index += 4;
  }

  uint8_t idLength = 0;
  if (hasIdLength) {
    if (index >= length) {
      return "truncated NDEF id length";
    }
    idLength = data[index++];
  }

  const uint32_t needed = static_cast<uint32_t>(index) + typeLength + idLength + payloadLength;
  if (needed > length) {
    return "truncated NDEF record bytes=" + String(length) + " expected=" + String(needed);
  }

  String type = asciiFromBytes(data + index, typeLength, typeLength);
  index += typeLength + idLength;
  const uint8_t *payload = data + index;

  if (tnf == 0x01 && type == "U" && payloadLength >= 1) {
    String uri = uriPrefix(payload[0]);
    uri += asciiFromBytes(payload + 1, payloadLength - 1, 72);
    return "uri " + uri;
  }

  if (tnf == 0x01 && type == "T" && payloadLength >= 1) {
    const uint8_t languageLength = payload[0] & 0x3F;
    if (payloadLength > static_cast<uint32_t>(1 + languageLength)) {
      return "text " + asciiFromBytes(payload + 1 + languageLength,
                                      payloadLength - 1 - languageLength, 72);
    }
  }

  return "ndef type=" + type + " bytes=" + String(payloadLength) +
         " hex=" + nfcBytesToHex(payload, payloadLength, 24);
}
