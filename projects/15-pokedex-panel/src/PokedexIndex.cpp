#include "PokedexIndex.h"

namespace pokedex {
namespace {

// The 18 type names that appear in index.csv, in the order the file uses them
// alphabetically. Index into this table is what Record::type1 stores.
const char *const kTypeNames[kTypeCount] = {
    "Bug",    "Dark",   "Dragon", "Electric", "Fairy",  "Fighting",
    "Fire",   "Flying", "Ghost",  "Grass",    "Ground", "Ice",
    "Normal", "Poison", "Psychic", "Rock",    "Steel",  "Water",
};

bool sameString(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) return false;
    a++;
    b++;
  }
  return *a == *b;
}

bool containsToken(const char *haystack, const char *needle) {
  if (haystack == nullptr || needle == nullptr) return false;
  for (const char *h = haystack; *h != '\0'; h++) {
    const char *a = h;
    const char *b = needle;
    while (*a != '\0' && *b != '\0' && *a == *b) {
      a++;
      b++;
    }
    if (*b == '\0') return true;
  }
  return false;
}

bool hasUnderscore(const char *text) { return containsToken(text, "_"); }

}  // namespace

uint8_t typeIdFromName(const char *name) {
  if (name == nullptr || name[0] == '\0') return kTypeAny;
  for (uint8_t i = 0; i < kTypeCount; i++) {
    if (sameString(name, kTypeNames[i])) return i;
  }
  return kTypeAny;
}

const char *typeNameFromId(uint8_t id) {
  if (id >= kTypeCount) return nullptr;
  return kTypeNames[id];
}

uint8_t classifyVariant(const char *entryId) {
  if (entryId == nullptr || entryId[0] == '\0') return kVariantBase;

  uint8_t flags = 0;
  if (containsToken(entryId, "_shadow")) flags |= kVariantShadow;
  if (containsToken(entryId, "_mega")) flags |= kVariantMega;
  if (containsToken(entryId, "_galarian") || containsToken(entryId, "_alolan") ||
      containsToken(entryId, "_hisuian") || containsToken(entryId, "_paldean")) {
    flags |= kVariantRegional;
  }
  if (flags != 0) return flags;
  return hasUnderscore(entryId) ? kVariantOther : kVariantBase;
}

}  // namespace pokedex
