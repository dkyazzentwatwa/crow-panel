#include "CardRfParser.h"

#include <stdlib.h>

namespace {

String protocolLine(const String &input) {
  String line = input;
  line.trim();
  int bracket = line.indexOf("] ");
  if (line.startsWith("[") && bracket >= 0) {
    line = line.substring(bracket + 2);
    line.trim();
  }
  return line;
}

String tokenValue(const String &line, const char *key) {
  String prefix = String(key) + "=";
  int cursor = 0;
  while (cursor < line.length()) {
    while (cursor < line.length() && line.charAt(cursor) == ' ') {
      cursor++;
    }
    int end = line.indexOf(' ', cursor);
    if (end < 0) {
      end = line.length();
    }
    String token = line.substring(cursor, end);
    if (token.startsWith(prefix)) {
      return token.substring(prefix.length());
    }
    cursor = end + 1;
  }
  return "";
}

bool parseUint32(const String &text, uint32_t &value) {
  if (text.length() == 0) {
    return false;
  }
  char *end = nullptr;
  unsigned long parsed = strtoul(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  value = (uint32_t)parsed;
  return true;
}

bool parseUint16(const String &text, uint16_t &value) {
  uint32_t parsed = 0;
  if (!parseUint32(text, parsed) || parsed > 65535UL) {
    return false;
  }
  value = (uint16_t)parsed;
  return true;
}

bool parseUint8(const String &text, uint8_t &value) {
  uint32_t parsed = 0;
  if (!parseUint32(text, parsed) || parsed > 255UL) {
    return false;
  }
  value = (uint8_t)parsed;
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

bool parseHexBins(const String &hex, uint8_t binCount, uint8_t *bins) {
  if (hex.length() != binCount * 2) {
    return false;
  }
  for (uint8_t i = 0; i < binCount; i++) {
    int hi = hexNibble(hex.charAt(i * 2));
    int lo = hexNibble(hex.charAt(i * 2 + 1));
    if (hi < 0 || lo < 0) {
      return false;
    }
    bins[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

}  // namespace

bool CardRfParser::parseLine(const String &input, CardRfLine &out, String &error) const {
  out = CardRfLine();
  error = "";
  String line = protocolLine(input);
  if (line.length() == 0) {
    error = "empty line";
    return false;
  }
  if (line.startsWith("SCANROW")) {
    return parseScanRow(line, out, error);
  }
  if (line.startsWith("POWER")) {
    return parsePower(line, out, error);
  }
  error = "expected SCANROW or POWER";
  return false;
}

bool CardRfParser::parseScanRow(const String &line, CardRfLine &out, String &error) const {
  CardRfScanRow row;
  if (!parseUint32(tokenValue(line, "START"), row.startHz)) {
    error = "SCANROW missing numeric START";
    return false;
  }
  if (!parseUint32(tokenValue(line, "STEP"), row.stepHz)) {
    error = "SCANROW missing numeric STEP";
    return false;
  }
  if (!parseUint8(tokenValue(line, "BINS"), row.binCount) || row.binCount == 0 ||
      row.binCount > kCardRfMaxBins) {
    error = "SCANROW BINS must be 1..32";
    return false;
  }
  if (!parseUint16(tokenValue(line, "MIN"), row.minPower)) {
    error = "SCANROW missing numeric MIN";
    return false;
  }
  if (!parseUint16(tokenValue(line, "MAX"), row.maxPower)) {
    error = "SCANROW missing numeric MAX";
    return false;
  }
  if (row.maxPower < row.minPower) {
    error = "SCANROW MAX below MIN";
    return false;
  }
  if (!parseHexBins(tokenValue(line, "DATA"), row.binCount, row.bins)) {
    error = "SCANROW DATA must be two hex chars per bin";
    return false;
  }

  out.kind = CARD_RF_LINE_SCANROW;
  out.scanRow = row;
  return true;
}

bool CardRfParser::parsePower(const String &line, CardRfLine &out, String &error) const {
  CardRfPowerSample sample;
  uint8_t clipped = 0;
  if (!parseUint16(tokenValue(line, "RAW"), sample.raw)) {
    error = "POWER missing numeric RAW";
    return false;
  }
  if (!parseUint8(tokenValue(line, "CLIP"), clipped) || clipped > 1) {
    error = "POWER CLIP must be 0 or 1";
    return false;
  }
  if (!parseUint16(tokenValue(line, "SAMPLES"), sample.samples)) {
    error = "POWER missing numeric SAMPLES";
    return false;
  }
  sample.clipped = clipped == 1;

  out.kind = CARD_RF_LINE_POWER;
  out.power = sample;
  return true;
}
