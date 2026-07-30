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

enum Order : uint8_t { kOrderDex = 0, kOrderName = 1 };

struct Filter {
  bool showShadows = false;
  uint8_t type1 = kTypeAny;
};

// Maps an index.csv type name ("Grass") to 0..17, or kTypeAny when unknown.
uint8_t typeIdFromName(const char *name);

// Reverse of typeIdFromName. Returns nullptr when id is out of range.
const char *typeNameFromId(uint8_t id);

// Classifies an entry_id into a Variant bitmask. Every marker is tested; a form
// carrying two markers gets both bits.
uint8_t classifyVariant(const char *entryId);

// Injected byte reader. Firmware implements this over an SD_MMC File; host tests
// implement it over a memory buffer. Keeps SD_MMC out of this translation unit.
class ByteSource {
 public:
  virtual ~ByteSource() {}
  virtual bool seek(uint32_t offset) = 0;
  virtual uint32_t read(uint8_t *dest, uint32_t length) = 0;
  virtual uint32_t size() const = 0;
};

class Index {
 public:
  // records/capacity and nameOrder are caller-owned. Passing nullptr for
  // nameOrder leaves name ordering unavailable, which is the documented
  // fallback when the PSRAM allocation for it fails.
  void attach(Record *records, uint16_t capacity, uint16_t *nameOrder);

  // Single pass over the CSV. Returns the number of rows parsed.
  uint16_t build(ByteSource &source);

  uint16_t rowCount() const { return rowCount_; }
  bool nameOrderAvailable() const { return nameOrder_ != nullptr; }
  const Record &record(uint16_t rowIndex) const { return records_[rowIndex]; }

  bool matches(uint16_t rowIndex, const Filter &filter) const;
  uint16_t countMatching(const Filter &filter) const;

  // Writes up to kGridPageSize row indices for `page` into outRows, which the
  // caller must size to at least kGridPageSize. Returns how many were written.
  uint8_t pageAt(uint16_t page, Order order, const Filter &filter,
                 uint16_t *outRows) const;

  // Always at least 1 so the UI can render "Page 1/1" for an empty filter.
  uint16_t pageCount(const Filter &filter) const;

 private:
  Record *records_ = nullptr;
  uint16_t capacity_ = 0;
  uint16_t *nameOrder_ = nullptr;
  uint16_t rowCount_ = 0;

  void buildNameOrder();
  uint16_t rowAtOrdinal(uint16_t ordinal, Order order) const;
};

}  // namespace pokedex

#endif
