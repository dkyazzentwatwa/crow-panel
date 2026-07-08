#ifndef POKEDEX_CATALOG_H
#define POKEDEX_CATALOG_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "PokedexTypes.h"

class PokedexCatalog {
 public:
  void begin();
  bool sdReady() const;
  bool indexReady() const;
  uint16_t totalRows() const;
  const String &status() const;
  const char *sourceLabel() const;

  uint8_t search(const String &query, PokedexRow *outRows, uint8_t maxRows, uint16_t &totalMatches);
  uint8_t browse(uint16_t start, PokedexRow *outRows, uint8_t maxRows);
  bool loadDetail(const PokedexRow &row, PokedexDetail &detail);
  bool findFirst(const String &query, PokedexRow &row);
  void printRows(Stream &out, const PokedexRow *rows, uint8_t count) const;

 private:
  bool sdReady_ = false;
  bool indexReady_ = false;
  uint16_t totalRows_ = 0;
  String status_ = "mock catalog";

  uint16_t countRows();
  uint8_t searchMock(const String &query, PokedexRow *outRows, uint8_t maxRows, uint16_t &totalMatches);
  uint8_t browseMock(uint16_t start, PokedexRow *outRows, uint8_t maxRows);
  bool loadMockDetail(const PokedexRow &row, PokedexDetail &detail);
  bool parseIndexLine(const String &line, PokedexRow &out) const;
  bool rowMatches(const PokedexRow &row, const String &query) const;
};

#endif
