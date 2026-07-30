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
// composing variants, and the longest observed 28-character name. 44 data
// rows on purpose -- more than kGridPageSize (18) -- with 23 of them starting
// with 'A' so the first 'B' (Bagon, ordinal 23) lands past the first window.
// That is precisely the case the old 8-row fixture could not exercise: a
// page-aligned window can only land on the page containing a jump target, not
// on the target itself.
const char *const kFixtureCsv =
    "dex,entry_id,name,type1,type2,file\n"
    "1,abra,Abra,Psychic,,abra.json\n"
    "2,abra_shadow,Abra Shadow,Psychic,,abra_shadow.json\n"
    "3,aerodactyl,Aerodactyl,Rock,Flying,aerodactyl.json\n"
    "4,aggron,Aggron,Steel,Rock,aggron.json\n"
    "5,alakazam,Alakazam,Psychic,,alakazam.json\n"
    "6,altaria,Altaria,Dragon,Flying,altaria.json\n"
    "7,ampharos,Ampharos,Electric,,ampharos.json\n"
    "8,anorith,Anorith,Rock,Bug,anorith.json\n"
    "9,arbok,Arbok,Poison,,arbok.json\n"
    "10,arcanine,Arcanine,Fire,,arcanine.json\n"
    "11,arceus,Arceus,Normal,,arceus.json\n"
    "12,archen,Archen,Rock,Flying,archen.json\n"
    "13,ariados,Ariados,Bug,Poison,ariados.json\n"
    "14,armaldo,Armaldo,Rock,Bug,armaldo.json\n"
    "15,aron,Aron,Steel,Rock,aron.json\n"
    "16,articuno,Articuno,Ice,Flying,articuno.json\n"
    "17,audino,Audino,Normal,,audino.json\n"
    "18,aurorus,Aurorus,Rock,Ice,aurorus.json\n"
    "19,avalugg,Avalugg,Ice,,avalugg.json\n"
    "20,axew,Axew,Dragon,,axew.json\n"
    "21,azelf,Azelf,Psychic,,azelf.json\n"
    "22,azumarill,Azumarill,Water,Fairy,azumarill.json\n"
    "23,azurill,Azurill,Normal,Fairy,azurill.json\n"
    "24,bagon,Bagon,Dragon,,bagon.json\n"
    "25,banette,Banette,Ghost,,banette.json\n"
    "26,bellsprout,Bellsprout,Grass,Poison,bellsprout.json\n"
    "27,bagon_shadow,Bagon Shadow,Dragon,,bagon_shadow.json\n"
    "28,charizard_mega_x,Charizard Mega X,Fire,Dragon,charizard_mega_x.json\n"
    "29,rattata_alolan_shadow,Rattata Alolan Shadow,Dark,Normal,rattata_alolan_shadow.json\n"
    "30,growlithe_hisuian,Growlithe Hisuian,Fire,Rock,growlithe_hisuian.json\n"
    "31,zigzagoon_galarian,Zigzagoon Galarian,Dark,Normal,zigzagoon_galarian.json\n"
    "32,charmander,Charmander,Fire,,charmander.json\n"
    "33,pikachu,Pikachu,Electric,,pikachu.json\n"
    "34,oddish,Oddish,Grass,Poison,oddish.json\n"
    "35,oddish_shadow,Oddish Shadow,Grass,Poison,oddish_shadow.json\n"
    "36,ekans,Ekans,Poison,,ekans.json\n"
    "37,sandshrew,Sandshrew,Ground,,sandshrew.json\n"
    "38,ninetales,Ninetales,Fire,,ninetales.json\n"
    "39,vulpix,Vulpix,Fire,,vulpix.json\n"
    "40,machop,Machop,Fighting,,machop.json\n"
    "41,gengar,Gengar,Ghost,Poison,gengar.json\n"
    "42,snorlax,Snorlax,Normal,,snorlax.json\n"
    "43,dragonite,Dragonite,Dragon,Flying,dragonite.json\n"
    "44,thundurus_incarnate_shadow,Thundurus (Incarnate) Shadow,Electric,Flying,x.json\n";

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
    Record records[64];
    uint16_t nameOrder[64];
    Index index;
    index.attach(records, 64, nameOrder);
    MemorySource source(kFixtureCsv);
    uint16_t parsed = index.build(source);

    expect("build parses 44 data rows, skipping the header", parsed == 44);
    expect("build reports rowCount", index.rowCount() == 44);
    expect("first record dex", index.record(0).dex == 1);
    expect("first record name", strcmp(index.record(0).name, "Abra") == 0);
    expect("first record type1 is Psychic", index.record(0).type1 == typeIdFromName("Psychic"));
    expect("first record is base", index.record(0).flags == kVariantBase);
    expect("second record is shadow", index.record(1).flags == kVariantShadow);
    expect("empty type2 does not break parse",
           strcmp(index.record(31).name, "Charmander") == 0);
    expect("composing variant survives parse",
           index.record(28).flags == (uint8_t)(kVariantRegional | kVariantShadow));
    expect("28-char name is not truncated",
           strcmp(index.record(43).name, "Thundurus (Incarnate) Shadow") == 0);
    expect("nameOrder availability reported", index.nameOrderAvailable());

    // Offsets must point at the start of each row so resolve() can seek to it.
    expect("row 0 offset points past the header",
           index.record(0).offset == (uint32_t)(strlen("dex,entry_id,name,type1,type2,file\n")));
    const char *at = kFixtureCsv + index.record(3).offset;
    expect("row 3 offset lands on its own line", strncmp(at, "4,aggron", 8) == 0);
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

  {
    Record records[64];
    uint16_t nameOrder[64];
    Index index;
    index.attach(records, 64, nameOrder);
    MemorySource source(kFixtureCsv);
    index.build(source);

    Filter all;
    all.showShadows = true;
    // 5 shadow rows: abra_shadow, bagon_shadow, rattata_alolan_shadow,
    // oddish_shadow, thundurus_incarnate_shadow.
    expect("countMatching with shadows shown", index.countMatching(all) == 44);

    Filter noShadow;
    noShadow.showShadows = false;
    expect("countMatching hides 5 shadow rows", index.countMatching(noShadow) == 39);

    Filter grass;
    grass.showShadows = true;
    grass.type1 = typeIdFromName("Grass");
    expect("type filter counts Grass rows", index.countMatching(grass) == 3);

    Filter grassNoShadow;
    grassNoShadow.type1 = typeIdFromName("Grass");
    expect("type and shadow filters compose", index.countMatching(grassNoShadow) == 2);

    Filter none;
    none.type1 = typeIdFromName("Fairy");  // never appears as type1 in the fixture.
    expect("filter matching nothing counts zero", index.countMatching(none) == 0);
    expect("pageCount is at least 1 when empty", index.pageCount(none) == 1);

    uint16_t rows[kGridPageSize];
    expect("empty filter yields an empty page",
           index.pageAt(0, kOrderDex, none, rows) == 0);

    uint8_t got = index.pageAt(0, kOrderDex, all, rows);
    expect("dex page returns a full first page", got == kGridPageSize);
    expect("dex order preserves file order", rows[0] == 0 && rows[1] == 1 && rows[17] == 17);

    got = index.pageAt(0, kOrderDex, noShadow, rows);
    expect("filtered page skips shadows", got == kGridPageSize);
    expect("filtered page starts at abra", rows[0] == 0);
    expect("filtered page second row is aerodactyl (abra_shadow skipped)", rows[1] == 2);

    expect("page past the end is empty",
           index.pageAt(9, kOrderDex, all, rows) == 0);
    expect("pageCount for 44 rows at 18 per page is 3", index.pageCount(all) == 3);

    // --- The offset-based primitive: this is what the original page-index
    // paging could never do, since a page-aligned window can only land on the
    // page containing a target, not on the target itself.
    expect("pageAtOrdinal(0) matches pageAt(0)",
           index.pageAtOrdinal(0, kOrderDex, all, rows) == kGridPageSize && rows[0] == 0);

    uint8_t tailGot = index.pageAtOrdinal(40, kOrderDex, all, rows);
    expect("pageAtOrdinal near the end returns a short window, not garbage",
           tailGot == 4);  // 44 total, ordinal 40 leaves rows 40..43 = 4 rows.

    expect("pageAtOrdinal(count) returns an empty window",
           index.pageAtOrdinal(index.countMatching(all), kOrderDex, all, rows) == 0);

    // Dex-ordinal jump: land exactly on the first row with dex >= 30.
    const uint32_t dexOrdinal = index.ordinalOfDex(30, all);
    expect("ordinalOfDex(30) is past the first window", dexOrdinal > kGridPageSize);
    uint8_t dexJumpGot = index.pageAtOrdinal(dexOrdinal, kOrderDex, all, rows);
    expect("dex ordinal jump lands the target at slot 0",
           dexJumpGot > 0 && index.record(rows[0]).dex >= 30);
  }

  {
    Record records[64];
    uint16_t nameOrder[64];
    Index index;
    index.attach(records, 64, nameOrder);
    MemorySource source(kFixtureCsv);
    index.build(source);

    Filter all;
    all.showShadows = true;

    uint16_t rows[kGridPageSize];
    index.pageAt(0, kOrderName, all, rows);
    // 23 names start with 'A' (ordinals 0..22), so the first name-ordered
    // page of 18 is entirely 'A' names: Abra .. Aurorus.
    expect("name order starts at Abra", strcmp(index.record(rows[0]).name, "Abra") == 0);
    expect("name order slot 1 is Abra Shadow",
           strcmp(index.record(rows[1]).name, "Abra Shadow") == 0);
    expect("first page of 18 is entirely A names",
           strcmp(index.record(rows[17]).name, "Aurorus") == 0);

    expect("hasLetter finds B", index.hasLetter('B', all));
    expect("hasLetter finds lowercase b", index.hasLetter('b', all));
    expect("hasLetter rejects Y (absent from the fixture)", !index.hasLetter('Y', all));

    // This is the exact bug from the plan: Bagon is the 24th matching name
    // (ordinal 23), well past the first 18-row window. The old page-index
    // jumpToLetter('B') landed on page 1 with 14 A-names ahead of Bagon; the
    // new ordinal primitive lands Bagon at slot 0 directly.
    const uint32_t bOrdinal = index.ordinalOfLetter('B', all);
    expect("ordinalOfLetter('B') is past the first window", bOrdinal == 23);
    expect("jumpToLetter B is now page 1, not page 0", index.jumpToLetter('B', all) == 1);

    uint8_t bGot = index.pageAtOrdinal(bOrdinal, kOrderName, all, rows);
    expect("pageAtOrdinal(ordinalOfLetter('B')) puts Bagon at slot 0",
           bGot > 0 && strcmp(index.record(rows[0]).name, "Bagon") == 0);

    expect("ordinalOfLetter('P') finds Pikachu at ordinal 37",
           index.ordinalOfLetter('P', all) == 37);
    expect("jumpToLetter P is page 2", index.jumpToLetter('P', all) == 2);

    Filter noShadow;
    expect("hasLetter respects the shadow filter", !index.hasLetter('R', noShadow));
    expect("hasLetter finds R with shadows shown", index.hasLetter('R', all));

    Filter grass;
    grass.showShadows = true;
    grass.type1 = typeIdFromName("Grass");
    expect("hasLetter respects the type filter", !index.hasLetter('P', grass));
    expect("hasLetter finds O under the Grass filter", index.hasLetter('O', grass));
    expect("ordinalOfLetter('Z') under Grass filter is one past the end",
           index.ordinalOfLetter('Z', grass) == index.countMatching(grass));

    expect("hasDexAtLeast finds 40+", index.hasDexAtLeast(40, all));
    expect("hasDexAtLeast rejects beyond the max", !index.hasDexAtLeast(100, all));
    expect("jumpToDex 0 is page 0", index.jumpToDex(0, all) == 0);

    // clampOrdinal: never runs past either end.
    expect("clampOrdinal clamps above the count",
           index.clampOrdinal(9999, all) == index.countMatching(all) - 1);
    expect("clampOrdinal passes through an in-range value",
           index.clampOrdinal(5, all) == 5);
    Filter empty;
    empty.type1 = typeIdFromName("Fairy");
    expect("clampOrdinal returns 0 for an empty filter", index.clampOrdinal(3, empty) == 0);
  }

  {
    // With no name-order buffer, name order must degrade to dex order rather
    // than read a null pointer.
    Record records[64];
    Index index;
    index.attach(records, 64, nullptr);
    MemorySource source(kFixtureCsv);
    index.build(source);

    Filter all;
    all.showShadows = true;
    uint16_t rows[kGridPageSize];
    index.pageAt(0, kOrderName, all, rows);
    expect("name order falls back to file order without a buffer", rows[0] == 0);
    expect("jumpToLetter is page 0 without a name order",
           index.jumpToLetter('T', all) == 0);
    expect("hasLetter is false without a name order", !index.hasLetter('B', all));
  }

  printf("\n%u failure(s)\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
