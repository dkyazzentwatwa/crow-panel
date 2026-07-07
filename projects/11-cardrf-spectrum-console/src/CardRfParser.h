#ifndef CARDRF_PARSER_H
#define CARDRF_PARSER_H

#include <Arduino.h>
#include "CardRfTypes.h"

class CardRfParser {
 public:
  bool parseLine(const String &line, CardRfLine &out, String &error) const;

 private:
  bool parseScanRow(const String &line, CardRfLine &out, String &error) const;
  bool parsePower(const String &line, CardRfLine &out, String &error) const;
};

#endif
