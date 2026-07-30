#ifndef POKEDEX_INDEX_H
#define POKEDEX_INDEX_H

// Pure C++ catalog index for the Pokedex panel. Deliberately free of Arduino.h,
// String, and SD_MMC so the identical translation unit compiles for the
// ESP32-P4 firmware and for the host harness in ../test (scripts/test-pokedex.sh).
#include <stdint.h>

namespace pokedex {

constexpr uint8_t kTypeCount = 18;
constexpr uint8_t kTypeAny = 0xFF;
constexpr uint8_t kGridPageSize = 18;
constexpr uint8_t kNameLength = 32;

// Variant markers parsed out of an entry_id. These COMPOSE — rattata_alolan_shadow
// is both regional and shadow — so this is a bitmask, never an enum value.
enum Variant : uint8_t {
  kVariantBase = 0x01,
  kVariantShadow = 0x02,
  kVariantMega = 0x04,
  kVariantRegional = 0x08,
  kVariantOther = 0x10,
};

struct Record {
  uint32_t offset;
  uint16_t dex;
  uint8_t flags;
  uint8_t type1;
  char name[kNameLength];
};

// Maps an index.csv type name ("Grass") to 0..17, or kTypeAny when unknown.
uint8_t typeIdFromName(const char *name);

// Reverse of typeIdFromName. Returns nullptr when id is out of range.
const char *typeNameFromId(uint8_t id);

// Classifies an entry_id into a Variant bitmask. Every marker is tested; a form
// carrying two markers gets both bits.
uint8_t classifyVariant(const char *entryId);

}  // namespace pokedex

#endif
