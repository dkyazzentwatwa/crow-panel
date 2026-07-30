// Host-side test harness for the pure Pokedex index and BMP decoder.
//
// Lives outside src/ on purpose: arduino-cli only compiles the sketch root and
// src/, so nothing in this folder ever reaches the firmware. Build and run it
// with scripts/test-pokedex.sh.
#include <stdio.h>
#include <string.h>

#include "../src/PokedexIndex.h"

using namespace pokedex;

namespace {

uint16_t gFailures = 0;

void expect(const char *name, bool ok) {
  printf("[host] %-52s %s\n", name, ok ? "PASS" : "FAIL");
  if (!ok) {
    gFailures++;
  }
}

// A ByteSource over a fixed string, standing in for the SD file in host tests.
class MemorySource : public ByteSource {
 public:
  MemorySource(const char *text) : text_(text), len_((uint32_t)strlen(text)) {}
  bool seek(uint32_t offset) override {
    if (offset > len_) return false;
    pos_ = offset;
    return true;
  }
  uint32_t read(uint8_t *dest, uint32_t length) override {
    uint32_t n = len_ - pos_;
    if (n > length) n = length;
    memcpy(dest, text_ + pos_, n);
    pos_ += n;
    return n;
  }
  uint32_t size() const override { return len_; }

 private:
  const char *text_;
  uint32_t len_;
  uint32_t pos_ = 0;
};

// Mirrors the real index.csv shape: header, dex order, empty type2 allowed,
// composing variants, and the longest observed 28-character name.
const char *const kFixtureCsv =
    "dex,entry_id,name,type1,type2,file\n"
    "1,bulbasaur,Bulbasaur,Grass,Poison,bulbasaur.json\n"
    "1,bulbasaur_shadow,Bulbasaur Shadow,Grass,Poison,bulbasaur_shadow.json\n"
    "4,charmander,Charmander,Fire,,charmander.json\n"
    "6,charizard_mega_x,Charizard Mega X,Fire,Dragon,charizard_mega_x.json\n"
    "19,rattata_alolan_shadow,Rattata Alolan Shadow,Dark,Normal,rattata_alolan_shadow.json\n"
    "25,pikachu,Pikachu,Electric,,pikachu.json\n"
    "43,oddish,Oddish,Grass,Poison,oddish.json\n"
    "641,thundurus_incarnate_shadow,Thundurus (Incarnate) Shadow,Electric,Flying,x.json\n";

}  // namespace

int main() {
  expect("Record is exactly 40 bytes", sizeof(Record) == 40);

  expect("typeIdFromName maps Bug to 0", typeIdFromName("Bug") == 0);
  expect("typeIdFromName maps Water to 17", typeIdFromName("Water") == 17);
  expect("typeIdFromName maps Grass to 9", typeIdFromName("Grass") == 9);
  expect("typeIdFromName rejects unknown", typeIdFromName("Cosmic") == kTypeAny);
  expect("typeIdFromName rejects empty", typeIdFromName("") == kTypeAny);
  expect("typeNameFromId round-trips Grass",
         strcmp(typeNameFromId(typeIdFromName("Grass")), "Grass") == 0);
  expect("typeNameFromId guards out of range", typeNameFromId(99) == nullptr);

  expect("classifyVariant plain name is base",
         classifyVariant("bulbasaur") == kVariantBase);
  expect("classifyVariant shadow", classifyVariant("bulbasaur_shadow") == kVariantShadow);
  expect("classifyVariant mega", classifyVariant("venusaur_mega") == kVariantMega);
  expect("classifyVariant mega x keeps mega bit",
         (classifyVariant("charizard_mega_x") & kVariantMega) != 0);
  expect("classifyVariant alolan is regional",
         classifyVariant("rattata_alolan") == kVariantRegional);
  expect("classifyVariant galarian is regional",
         classifyVariant("zigzagoon_galarian") == kVariantRegional);
  expect("classifyVariant hisuian is regional",
         classifyVariant("growlithe_hisuian") == kVariantRegional);
  expect("classifyVariant alolan shadow composes both",
         classifyVariant("rattata_alolan_shadow") ==
             (uint8_t)(kVariantRegional | kVariantShadow));
  expect("classifyVariant costume form is other",
         classifyVariant("pikachu_pop_star") == kVariantOther);
  expect("classifyVariant anniversary form is other",
         classifyVariant("pikachu_5th_anniversary") == kVariantOther);
  expect("classifyVariant null is base", classifyVariant(nullptr) == kVariantBase);

  {
    Record records[16];
    uint16_t nameOrder[16];
    Index index;
    index.attach(records, 16, nameOrder);
    MemorySource source(kFixtureCsv);
    uint16_t parsed = index.build(source);

    expect("build parses 8 data rows, skipping the header", parsed == 8);
    expect("build reports rowCount", index.rowCount() == 8);
    expect("first record dex", index.record(0).dex == 1);
    expect("first record name", strcmp(index.record(0).name, "Bulbasaur") == 0);
    expect("first record type1 is Grass", index.record(0).type1 == typeIdFromName("Grass"));
    expect("first record is base", index.record(0).flags == kVariantBase);
    expect("second record is shadow", index.record(1).flags == kVariantShadow);
    expect("empty type2 does not break parse",
           strcmp(index.record(2).name, "Charmander") == 0);
    expect("composing variant survives parse",
           index.record(4).flags == (uint8_t)(kVariantRegional | kVariantShadow));
    expect("28-char name is not truncated",
           strcmp(index.record(7).name, "Thundurus (Incarnate) Shadow") == 0);
    expect("nameOrder availability reported", index.nameOrderAvailable());

    // Offsets must point at the start of each row so resolve() can seek to it.
    expect("row 0 offset points past the header",
           index.record(0).offset == (uint32_t)(strlen("dex,entry_id,name,type1,type2,file\n")));
    const char *at = kFixtureCsv + index.record(3).offset;
    expect("row 3 offset lands on its own line", strncmp(at, "6,charizard_mega_x", 18) == 0);
  }

  {
    // Capacity smaller than the file must stop cleanly, not overrun.
    Record records[3];
    Index index;
    index.attach(records, 3, nullptr);
    MemorySource source(kFixtureCsv);
    expect("build respects capacity", index.build(source) == 3);
    expect("no name order without a buffer", !index.nameOrderAvailable());
  }

  {
    // A file with only a header yields zero rows rather than a bogus row.
    Record records[4];
    Index index;
    index.attach(records, 4, nullptr);
    MemorySource header("dex,entry_id,name,type1,type2,file\n");
    expect("header-only file parses to zero rows", index.build(header) == 0);
  }

  printf("\n%u failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
