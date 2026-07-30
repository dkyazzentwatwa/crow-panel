#ifndef POKEDEX_CATALOG_H
#define POKEDEX_CATALOG_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "PokedexTypes.h"
#include "PokedexIndex.h"

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

  // Index-backed queries. When the index is unavailable these fall back to the
  // pre-existing linear scan so the project stays usable.
  //
  // Browse is OFFSET-based, not page-based: a jump sets the offset so the target
  // lands at grid slot 0. A page-aligned window cannot do that - measured on the
  // real catalog, tapping "B" left Bagon at slot 14 behind fourteen A-names.
  bool indexActive() const { return indexRows_ > 0; }
  uint16_t indexRowCount() const { return indexRows_; }
  bool nameOrderActive() const;
  uint16_t countMatching(const pokedex::Filter &filter) const;
  uint8_t window(uint32_t startOrdinal, pokedex::Order order,
                 const pokedex::Filter &filter, PokedexRow *outRows, uint8_t maxRows);
  uint32_t ordinalOfLetter(char letter, const pokedex::Filter &filter) const;
  bool hasLetter(char letter, const pokedex::Filter &filter) const;
  uint32_t ordinalOfDex(uint16_t dex, const pokedex::Filter &filter) const;
  bool hasDexAtLeast(uint16_t dex, const pokedex::Filter &filter) const;
  uint32_t clampOrdinal(uint32_t ordinal, const pokedex::Filter &filter) const;

 private:
  bool sdReady_ = false;
  bool indexReady_ = false;
  uint16_t totalRows_ = 0;
  String status_ = "mock catalog";

  pokedex::Index index_;
  pokedex::Record *indexRecords_ = nullptr;
  uint16_t *indexNameOrder_ = nullptr;
  uint16_t indexRows_ = 0;

  uint16_t countRows();
  uint8_t searchMock(const String &query, PokedexRow *outRows, uint8_t maxRows, uint16_t &totalMatches);
  uint8_t browseMock(uint16_t start, PokedexRow *outRows, uint8_t maxRows);
  bool loadMockDetail(const PokedexRow &row, PokedexDetail &detail);
  bool parseIndexLine(const String &line, PokedexRow &out) const;
  bool rowMatches(const PokedexRow &row, const String &query) const;

  void buildIndex();
  void releaseIndex();
};

#endif
