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

}  // namespace pokedex
