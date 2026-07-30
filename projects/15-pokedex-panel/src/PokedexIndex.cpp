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

// Copies field `index` (0-based, comma separated) out of `line` into `out`.
// index.csv has no quoted fields and no embedded commas, so a plain splitter is
// correct here.
bool fieldAt(const char *line, uint8_t index, char *out, uint16_t outSize) {
  uint8_t field = 0;
  const char *start = line;
  for (const char *p = line;; p++) {
    if (*p != ',' && *p != '\0') continue;
    if (field == index) {
      uint16_t len = (uint16_t)(p - start);
      if (len > outSize - 1) len = outSize - 1;
      for (uint16_t i = 0; i < len; i++) out[i] = start[i];
      out[len] = '\0';
      return true;
    }
    if (*p == '\0') return false;
    field++;
    start = p + 1;
  }
}

uint16_t parseDex(const char *text) {
  uint32_t value = 0;
  for (const char *p = text; *p >= '0' && *p <= '9'; p++) {
    value = value * 10 + (uint32_t)(*p - '0');
    if (value > 65535) return 0;
  }
  return (uint16_t)value;
}

// Returns false for the header row, which parses to dex 0 and is thus rejected
// without a special case.
bool parseLine(const char *line, uint32_t offset, Record &out) {
  char field[72];
  if (!fieldAt(line, 0, field, sizeof(field))) return false;
  uint16_t dex = parseDex(field);
  if (dex == 0) return false;
  out.dex = dex;
  out.offset = offset;
  if (!fieldAt(line, 1, field, sizeof(field))) return false;
  out.flags = classifyVariant(field);
  if (!fieldAt(line, 2, out.name, kNameLength)) return false;
  if (!fieldAt(line, 3, field, sizeof(field))) return false;
  out.type1 = typeIdFromName(field);
  return true;
}

char upperChar(char c) {
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

// Case-insensitive compare so the A-Z rail groups names the way a reader
// expects regardless of how index.csv capitalises them.
int compareNames(const char *a, const char *b) {
  for (;; a++, b++) {
    const char ca = upperChar(*a);
    const char cb = upperChar(*b);
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ca == '\0') return 0;
  }
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

void Index::attach(Record *records, uint16_t capacity, uint16_t *nameOrder) {
  records_ = records;
  capacity_ = capacity;
  nameOrder_ = nameOrder;
  rowCount_ = 0;
}

uint16_t Index::build(ByteSource &source) {
  rowCount_ = 0;
  if (records_ == nullptr || capacity_ == 0) return 0;
  if (!source.seek(0)) return 0;

  uint8_t chunk[512];
  char line[256];
  uint32_t chunkLen = 0;
  uint32_t chunkPos = 0;
  uint32_t absolute = 0;
  uint32_t lineStart = 0;
  uint16_t lineLen = 0;

  for (;;) {
    if (chunkPos >= chunkLen) {
      chunkLen = source.read(chunk, sizeof(chunk));
      chunkPos = 0;
      if (chunkLen == 0) break;
    }
    char c = (char)chunk[chunkPos++];
    absolute++;
    if (c == '\r') continue;
    if (c != '\n') {
      if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
      continue;
    }
    line[lineLen] = '\0';
    if (lineLen > 0 && rowCount_ < capacity_ &&
        parseLine(line, lineStart, records_[rowCount_])) {
      rowCount_++;
    }
    lineLen = 0;
    lineStart = absolute;
    if (rowCount_ >= capacity_) break;
  }

  // A final row with no trailing newline still counts.
  if (lineLen > 0 && rowCount_ < capacity_) {
    line[lineLen] = '\0';
    if (parseLine(line, lineStart, records_[rowCount_])) rowCount_++;
  }

  buildNameOrder();
  return rowCount_;
}

void Index::buildNameOrder() {
  if (nameOrder_ == nullptr) return;
  for (uint16_t i = 0; i < rowCount_; i++) nameOrder_[i] = i;

  // Binary insertion sort. Deliberately dependency-free and stable; boot-time
  // cost on 1573 rows must be measured on hardware, not assumed.
  for (uint16_t i = 1; i < rowCount_; i++) {
    const uint16_t value = nameOrder_[i];
    uint16_t low = 0;
    uint16_t high = i;
    while (low < high) {
      const uint16_t mid = (uint16_t)(low + (high - low) / 2);
      const int cmp = compareNames(records_[value].name, records_[nameOrder_[mid]].name);
      if (cmp < 0 || (cmp == 0 && records_[value].dex < records_[nameOrder_[mid]].dex)) {
        high = mid;
      } else {
        low = (uint16_t)(mid + 1);
      }
    }
    for (uint16_t j = i; j > low; j--) nameOrder_[j] = nameOrder_[j - 1];
    nameOrder_[low] = value;
  }
}

uint16_t Index::rowAtOrdinal(uint16_t ordinal, Order order) const {
  if (order == kOrderName && nameOrder_ != nullptr) return nameOrder_[ordinal];
  return ordinal;  // index.csv is already in dex order.
}

bool Index::matches(uint16_t rowIndex, const Filter &filter) const {
  if (rowIndex >= rowCount_) return false;
  const Record &row = records_[rowIndex];
  if (!filter.showShadows && (row.flags & kVariantShadow) != 0) return false;
  if (filter.type1 != kTypeAny && row.type1 != filter.type1) return false;
  return true;
}

uint16_t Index::countMatching(const Filter &filter) const {
  uint16_t total = 0;
  for (uint16_t i = 0; i < rowCount_; i++) {
    if (matches(i, filter)) total++;
  }
  return total;
}

uint8_t Index::pageAt(uint16_t page, Order order, const Filter &filter,
                      uint16_t *outRows) const {
  const uint32_t skip = (uint32_t)page * kGridPageSize;
  uint32_t seen = 0;
  uint8_t written = 0;
  for (uint16_t i = 0; i < rowCount_ && written < kGridPageSize; i++) {
    const uint16_t row = rowAtOrdinal(i, order);
    if (!matches(row, filter)) continue;
    if (seen++ < skip) continue;
    outRows[written++] = row;
  }
  return written;
}

uint16_t Index::pageCount(const Filter &filter) const {
  const uint16_t total = countMatching(filter);
  if (total == 0) return 1;
  return (uint16_t)((total + kGridPageSize - 1) / kGridPageSize);
}

uint16_t Index::jumpToLetter(char letter, const Filter &filter) const {
  if (nameOrder_ == nullptr) return 0;
  const char want = upperChar(letter);
  uint32_t ordinal = 0;
  for (uint16_t i = 0; i < rowCount_; i++) {
    const uint16_t row = nameOrder_[i];
    if (!matches(row, filter)) continue;
    if (upperChar(records_[row].name[0]) >= want) {
      return (uint16_t)(ordinal / kGridPageSize);
    }
    ordinal++;
  }
  if (ordinal == 0) return 0;
  return (uint16_t)((ordinal - 1) / kGridPageSize);
}

bool Index::hasLetter(char letter, const Filter &filter) const {
  if (nameOrder_ == nullptr) return false;
  const char want = upperChar(letter);
  for (uint16_t i = 0; i < rowCount_; i++) {
    const uint16_t row = nameOrder_[i];
    if (!matches(row, filter)) continue;
    if (upperChar(records_[row].name[0]) == want) return true;
  }
  return false;
}

uint16_t Index::jumpToDex(uint16_t dex, const Filter &filter) const {
  uint32_t ordinal = 0;
  for (uint16_t i = 0; i < rowCount_; i++) {
    if (!matches(i, filter)) continue;
    if (records_[i].dex >= dex) return (uint16_t)(ordinal / kGridPageSize);
    ordinal++;
  }
  if (ordinal == 0) return 0;
  return (uint16_t)((ordinal - 1) / kGridPageSize);
}

bool Index::hasDexAtLeast(uint16_t dex, const Filter &filter) const {
  for (uint16_t i = 0; i < rowCount_; i++) {
    if (matches(i, filter) && records_[i].dex >= dex) return true;
  }
  return false;
}

}  // namespace pokedex
