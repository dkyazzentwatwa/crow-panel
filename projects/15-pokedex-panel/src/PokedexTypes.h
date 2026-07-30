#ifndef POKEDEX_TYPES_H
#define POKEDEX_TYPES_H

#include <Arduino.h>

// Matches pokedex::kGridPageSize so one grid window fits in one result buffer.
constexpr uint8_t POKEDEX_MAX_RESULTS = 18;
constexpr uint8_t POKEDEX_DETAIL_PAGE_COUNT = 5;

struct PokedexRow {
  char dex[8] = "";
  char entryId[48] = "";
  char name[56] = "";
  char type1[16] = "";
  char type2[16] = "";
  char file[64] = "";
};

struct PokedexDetail {
  bool loaded = false;
  bool fromSd = false;
  char dex[8] = "";
  char entryId[48] = "";
  char name[56] = "";
  char type1[16] = "";
  char type2[16] = "";
  char weaknesses[144] = "";
  char resistances[144] = "";
  char evolution[144] = "";
  char fastMoves[144] = "";
  char chargedMoves[176] = "";
  char tags[128] = "";
  char trainerNote[260] = "";
  char sourceFile[72] = "";
  uint16_t atk = 0;
  uint16_t def = 0;
  uint16_t hp = 0;
  uint8_t buddyKm = 0;
  uint32_t secondMoveStardust = 0;
};

enum PokedexUiMode {
  kPokedexUiList,
  kPokedexUiDetail,
  kPokedexUiSearch
};

enum PokedexUiEventType {
  kPokedexUiNone,
  kPokedexUiSelectRow,
  kPokedexUiBackToList,
  kPokedexUiNextPage,
  kPokedexUiPrevPage,
  kPokedexUiBrowsePrev,
  kPokedexUiBrowseNext,
  kPokedexUiOpenSearch,
  kPokedexUiSearchSubmit,
  kPokedexUiSearchCancel
};

struct PokedexUiEvent {
  PokedexUiEventType type = kPokedexUiNone;
  uint8_t row = 0;
  String text;
};

#endif
