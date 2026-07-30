#include "PokedexCatalog.h"

// Include SD_MMC.h directly under the flag - do NOT wrap it in __has_include.
// arduino-cli decides which libraries to link by preprocessing sources before
// SD_MMC is on the include path, so a __has_include guard evaluates false
// during discovery, the library never gets linked, and the feature silently
// compiles out even with USE_SD_POKEDEX=1.
#if USE_SD_POKEDEX
#include <SD_MMC.h>
#define POKEDEX_HAS_SD_MMC 1
#include <ArduinoJson.h>
#else
#define POKEDEX_HAS_SD_MMC 0
#endif

#include <CrowPanelShared.h>

#include "PokedexSdSource.h"

namespace {

struct MockPokemon {
  PokedexRow row;
  const char *weaknesses;
  const char *resistances;
  const char *evolution;
  const char *fastMoves;
  const char *chargedMoves;
  const char *tags;
  const char *trainerNote;
  uint16_t atk;
  uint16_t def;
  uint16_t hp;
  uint8_t buddyKm;
  uint32_t secondMoveStardust;
};

const MockPokemon kMockPokemon[] = {
    {{"1", "bulbasaur", "Bulbasaur", "Grass", "Poison", "bulbasaur.json"},
     "Fire x1.6, Flying x1.6, Ice x1.6, Psychic x1.6",
     "Electric x0.62, Fairy x0.62, Fighting x0.62, Grass x0.39, Water x0.62",
     "Bulbasaur, Ivysaur, Venusaur", "Tackle, Vine Whip",
     "Power Whip, Seed Bomb, Sludge Bomb", "starter, grass, kanto",
     "Balanced starter that teaches type coverage and evolution paths cleanly.", 118, 111, 128, 3,
     10000},
    {{"6", "charizard_mega_x", "Charizard (Mega X)", "Fire", "Dragon", "charizard_mega_x.json"},
     "Dragon x1.6, Ground x1.6, Rock x1.6",
     "Bug x0.39, Electric x0.62, Fire x0.39, Grass x0.39, Steel x0.62",
     "Charmander, Charmeleon, Charizard, Mega X", "Fire Spin, Wing Attack",
     "Blast Burn, Dragon Claw, Overheat", "mega, starter, fire",
     "Big showcase card: strong colors, familiar name, and a clean dual-type story.", 273, 213, 186, 3,
     10000},
    {{"25", "pikachu", "Pikachu", "Electric", "", "pikachu.json"},
     "Ground x1.6", "Electric x0.62, Flying x0.62, Steel x0.62",
     "Pichu, Pikachu, Raichu", "Present, Quick Attack, Thunder Shock",
     "Discharge, Surf, Thunder, Thunderbolt, Wild Charge", "mascot, electric, kanto",
     "Small, readable, and iconic. Perfect for testing search and detail layouts.", 112, 96, 111, 1,
     10000},
    {{"94", "gengar_mega", "Gengar (Mega)", "Ghost", "Poison", "gengar_mega.json"},
     "Dark x1.6, Ghost x1.6, Ground x1.6, Psychic x1.6",
     "Bug x0.39, Fairy x0.62, Fighting x0.24, Grass x0.62, Normal x0.39, Poison x0.39",
     "Gastly, Haunter, Gengar, Mega", "Hex, Lick, Shadow Claw",
     "Focus Blast, Shadow Ball, Sludge Bomb", "mega, ghost, glass cannon",
     "Fast-pressure attacker with a weakness list that makes the panel feel useful.", 349, 199, 155, 3,
     50000},
    {{"133", "eevee", "Eevee", "Normal", "", "eevee.json"},
     "Fighting x1.6", "Ghost x0.39",
     "Eevee, Vaporeon, Jolteon, Flareon, Espeon, Umbreon, Leafeon, Glaceon, Sylveon",
     "Quick Attack, Tackle", "Body Slam, Dig, Swift", "evolution, buddy, normal",
     "A great browse-row stress test because the evolution line is intentionally long.", 104, 114,
     146, 5, 75000},
    {{"150", "mewtwo_shadow", "Mewtwo Shadow", "Psychic", "", "mewtwo_shadow.json"},
     "Bug x1.6, Dark x1.6, Ghost x1.6", "Fighting x0.62, Psychic x0.62",
     "Mewtwo", "Confusion, Psycho Cut",
     "Focus Blast, Ice Beam, Psystrike, Shadow Ball, Thunderbolt", "legendary, shadow, raid",
     "High-drama detail card for showing stats, move pressure, and shadow variant search.", 300, 182,
     214, 20, 100000},
    {{"448", "lucario", "Lucario", "Fighting", "Steel", "lucario.json"},
     "Fighting x1.6, Fire x1.6, Ground x1.6",
     "Bug x0.39, Dark x0.62, Dragon x0.62, Grass x0.62, Ice x0.62, Normal x0.62, Poison x0.39, Rock x0.39, Steel x0.62",
     "Riolu, Lucario", "Bullet Punch, Counter",
     "Aura Sphere, Blaze Kick, Close Combat, Power-Up Punch, Shadow Ball", "fighter, steel, raid",
     "Strong utility card: many resistances, punchy moves, and instantly recognizable typing.", 236, 144,
     172, 5, 75000},
    {{"384", "rayquaza_mega", "Rayquaza (Mega)", "Dragon", "Flying", "rayquaza_mega.json"},
     "Ice x2.56, Dragon x1.6, Fairy x1.6, Rock x1.6",
     "Bug x0.62, Fighting x0.62, Fire x0.62, Grass x0.39, Ground x0.39, Water x0.62",
     "Rayquaza, Mega", "Air Slash, Dragon Tail",
     "Aerial Ace, Breaking Swipe, Dragon Ascent, Outrage", "legendary, mega, dragon",
     "The poster-card for a big 7-inch panel: huge attack and a clear double-ice warning.", 377,
     210, 227, 20, 100000},
};

constexpr uint8_t kMockPokemonCount = sizeof(kMockPokemon) / sizeof(kMockPokemon[0]);

void copyString(char *dest, size_t len, const String &value) {
  if (len == 0) return;
  value.toCharArray(dest, len);
  dest[len - 1] = '\0';
}

void copyString(char *dest, size_t len, const char *value) {
  copyString(dest, len, String(value ? value : ""));
}

String lowerTrimmed(String value) {
  value.trim();
  value.toLowerCase();
  return value;
}

String normalizedDex(String value) {
  value.trim();
  while (value.length() > 1 && value[0] == '0') {
    value.remove(0, 1);
  }
  return value;
}

#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
void joinJsonArray(JsonDocument &doc, const char *key, char *dest, size_t len) {
  String out;
  JsonArrayConst values = doc[key].as<JsonArrayConst>();
  for (JsonVariantConst value : values) {
    const char *s = value.as<const char *>();
    if (s == nullptr || s[0] == '\0') continue;
    if (out.length() > 0) out += ", ";
    out += s;
  }
  if (out.length() == 0) out = "-";
  copyString(dest, len, out);
}
#endif

}  // namespace

void PokedexCatalog::begin() {
  sdReady_ = false;
  indexReady_ = false;
  totalRows_ = kMockPokemonCount;
  status_ = "mock catalog ready";

#if USE_SD_POKEDEX
#if POKEDEX_HAS_SD_MMC
  if (!SD_MMC.begin("/sdcard", POKEDEX_SDMMC_1BIT != 0)) {
    status_ = "SD MOUNT FAILED; using mock catalog";
    Logger::error("pokedex", status_);
    return;
  }

  sdReady_ = true;
  indexReady_ = SD_MMC.exists(POKEDEX_INDEX_PATH);
  if (!indexReady_) {
    status_ = String("MISSING ") + POKEDEX_INDEX_PATH + "; using mock catalog";
    Logger::error("pokedex", status_);
    sdReady_ = false;
    totalRows_ = kMockPokemonCount;
    return;
  }

  totalRows_ = countRows();
  if (totalRows_ == 0) {
    status_ = String("EMPTY ") + POKEDEX_INDEX_PATH + "; using mock catalog";
    Logger::error("pokedex", status_);
    sdReady_ = false;
    indexReady_ = false;
    totalRows_ = kMockPokemonCount;
    return;
  }
  status_ = "SD catalog // " + String(totalRows_) + " entries";
  Logger::info("pokedex", status_);
  buildIndex();
#else
  status_ = "SD_MMC.h unavailable; using mock catalog";
  Logger::error("pokedex", status_);
#endif
#else
  status_ = "SD disabled at build; using mock catalog";
  Logger::info("pokedex", status_);
#endif
}

bool PokedexCatalog::sdReady() const {
  return sdReady_;
}

bool PokedexCatalog::indexReady() const {
  return indexReady_;
}

uint16_t PokedexCatalog::totalRows() const {
  return totalRows_;
}

const String &PokedexCatalog::status() const {
  return status_;
}

const char *PokedexCatalog::sourceLabel() const {
  return (sdReady_ && indexReady_) ? "SD catalog" : "mock catalog";
}

uint16_t PokedexCatalog::countRows() {
#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  File index = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
  if (!index) return 0;

  uint16_t count = 0;
  bool firstLine = true;
  while (index.available()) {
    String line = index.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (firstLine) {
      firstLine = false;
      if (line.startsWith("dex,")) continue;
    }
    count++;
  }
  index.close();
  return count;
#else
  return kMockPokemonCount;
#endif
}

uint8_t PokedexCatalog::search(const String &query, PokedexRow *outRows, uint8_t maxRows,
                               uint16_t &totalMatches) {
  totalMatches = 0;
  if (outRows == nullptr || maxRows == 0) return 0;

#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  if (sdReady_ && indexReady_) {
    File index = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
    if (!index) {
      status_ = "SD INDEX OPEN FAILED; using mock catalog";
      Logger::error("pokedex", status_);
      sdReady_ = false;
      indexReady_ = false;
      totalRows_ = kMockPokemonCount;
      return searchMock(query, outRows, maxRows, totalMatches);
    }

    uint8_t count = 0;
    bool firstLine = true;
    while (index.available()) {
      String line = index.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      if (firstLine) {
        firstLine = false;
        if (line.startsWith("dex,")) continue;
      }
      PokedexRow row;
      if (parseIndexLine(line, row) && rowMatches(row, query)) {
        totalMatches++;
        if (count < maxRows) {
          outRows[count++] = row;
        }
      }
    }
    index.close();
    return count;
  }
#endif

  return searchMock(query, outRows, maxRows, totalMatches);
}

uint8_t PokedexCatalog::browse(uint16_t start, PokedexRow *outRows, uint8_t maxRows) {
  if (outRows == nullptr || maxRows == 0) return 0;

#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  if (sdReady_ && indexReady_) {
    File index = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
    if (!index) {
      status_ = "SD INDEX OPEN FAILED; using mock catalog";
      Logger::error("pokedex", status_);
      sdReady_ = false;
      indexReady_ = false;
      totalRows_ = kMockPokemonCount;
      return browseMock(start, outRows, maxRows);
    }

    uint8_t count = 0;
    uint16_t rowIndex = 0;
    bool firstLine = true;
    while (index.available() && count < maxRows) {
      String line = index.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      if (firstLine) {
        firstLine = false;
        if (line.startsWith("dex,")) continue;
      }
      if (rowIndex >= start) {
        parseIndexLine(line, outRows[count]);
        count++;
      }
      rowIndex++;
    }
    index.close();
    return count;
  }
#endif

  return browseMock(start, outRows, maxRows);
}

bool PokedexCatalog::loadDetail(const PokedexRow &row, PokedexDetail &detail) {
  detail = PokedexDetail();

#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  if (sdReady_ && indexReady_ && row.file[0] != '\0') {
    String path = String(POKEDEX_DATA_DIR) + row.file;
    File data = SD_MMC.open(path.c_str(), FILE_READ);
    if (!data) {
      status_ = String("SD DETAIL MISSING: ") + path;
      Logger::error("pokedex", status_);
      return loadMockDetail(row, detail);
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data);
    data.close();
    if (err) {
      status_ = String("SD DETAIL JSON INVALID: ") + path;
      Logger::error("pokedex", status_);
      return loadMockDetail(row, detail);
    }

    detail.loaded = true;
    detail.fromSd = true;
    copyString(detail.dex, sizeof(detail.dex), String(doc["dex"] | row.dex));
    copyString(detail.entryId, sizeof(detail.entryId), doc["entry_id"] | row.entryId);
    copyString(detail.name, sizeof(detail.name), doc["name"] | row.name);
    JsonArrayConst types = doc["types"].as<JsonArrayConst>();
    copyString(detail.type1, sizeof(detail.type1),
               types.size() > 0 ? (types[0] | row.type1) : row.type1);
    copyString(detail.type2, sizeof(detail.type2),
               types.size() > 1 ? (types[1] | row.type2) : row.type2);
    joinJsonArray(doc, "weaknesses", detail.weaknesses, sizeof(detail.weaknesses));
    joinJsonArray(doc, "resistances", detail.resistances, sizeof(detail.resistances));
    joinJsonArray(doc, "evolution", detail.evolution, sizeof(detail.evolution));
    joinJsonArray(doc, "fast_moves", detail.fastMoves, sizeof(detail.fastMoves));
    joinJsonArray(doc, "charged_moves", detail.chargedMoves, sizeof(detail.chargedMoves));
    joinJsonArray(doc, "tags", detail.tags, sizeof(detail.tags));
    JsonObjectConst stats = doc["go_stats"].as<JsonObjectConst>();
    detail.atk = stats["atk"] | 0;
    detail.def = stats["def"] | 0;
    detail.hp = stats["hp"] | 0;
    detail.buddyKm = doc["buddy_km"] | 0;
    detail.secondMoveStardust = doc["second_move_stardust"] | 0;
    copyString(detail.trainerNote, sizeof(detail.trainerNote),
               doc["trainer_note"] | "No trainer note in this record.");
    copyString(detail.sourceFile, sizeof(detail.sourceFile), path);
    return true;
  }
#endif

  return loadMockDetail(row, detail);
}

bool PokedexCatalog::findFirst(const String &query, PokedexRow &row) {
  PokedexRow one[1];
  uint16_t total = 0;
  uint8_t count = search(query, one, 1, total);
  if (count == 0) return false;
  row = one[0];
  return true;
}

void PokedexCatalog::printRows(Stream &out, const PokedexRow *rows, uint8_t count) const {
  for (uint8_t i = 0; i < count; i++) {
    out.print(i + 1);
    out.print(F(". #"));
    out.print(rows[i].dex);
    out.print(F(" "));
    out.print(rows[i].name);
    out.print(F(" ["));
    out.print(rows[i].type1);
    if (rows[i].type2[0] != '\0') {
      out.print(F("/"));
      out.print(rows[i].type2);
    }
    out.print(F("] "));
    out.println(rows[i].entryId);
  }
}

uint8_t PokedexCatalog::searchMock(const String &query, PokedexRow *outRows, uint8_t maxRows,
                                   uint16_t &totalMatches) {
  totalMatches = 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < kMockPokemonCount; i++) {
    if (rowMatches(kMockPokemon[i].row, query)) {
      totalMatches++;
      if (count < maxRows) {
        outRows[count++] = kMockPokemon[i].row;
      }
    }
  }
  return count;
}

uint8_t PokedexCatalog::browseMock(uint16_t start, PokedexRow *outRows, uint8_t maxRows) {
  if (start >= kMockPokemonCount) start = 0;
  uint8_t count = 0;
  for (uint8_t i = start; i < kMockPokemonCount && count < maxRows; i++) {
    outRows[count++] = kMockPokemon[i].row;
  }
  return count;
}

bool PokedexCatalog::loadMockDetail(const PokedexRow &row, PokedexDetail &detail) {
  const MockPokemon *source = nullptr;
  for (uint8_t i = 0; i < kMockPokemonCount; i++) {
    if (strcmp(row.entryId, kMockPokemon[i].row.entryId) == 0 ||
        strcmp(row.name, kMockPokemon[i].row.name) == 0) {
      source = &kMockPokemon[i];
      break;
    }
  }
  if (source == nullptr) {
    source = &kMockPokemon[0];
  }

  detail.loaded = true;
  detail.fromSd = false;
  copyString(detail.dex, sizeof(detail.dex), source->row.dex);
  copyString(detail.entryId, sizeof(detail.entryId), source->row.entryId);
  copyString(detail.name, sizeof(detail.name), source->row.name);
  copyString(detail.type1, sizeof(detail.type1), source->row.type1);
  copyString(detail.type2, sizeof(detail.type2), source->row.type2);
  copyString(detail.weaknesses, sizeof(detail.weaknesses), source->weaknesses);
  copyString(detail.resistances, sizeof(detail.resistances), source->resistances);
  copyString(detail.evolution, sizeof(detail.evolution), source->evolution);
  copyString(detail.fastMoves, sizeof(detail.fastMoves), source->fastMoves);
  copyString(detail.chargedMoves, sizeof(detail.chargedMoves), source->chargedMoves);
  copyString(detail.tags, sizeof(detail.tags), source->tags);
  copyString(detail.trainerNote, sizeof(detail.trainerNote), source->trainerNote);
  copyString(detail.sourceFile, sizeof(detail.sourceFile), String("mock://") + source->row.entryId);
  detail.atk = source->atk;
  detail.def = source->def;
  detail.hp = source->hp;
  detail.buddyKm = source->buddyKm;
  detail.secondMoveStardust = source->secondMoveStardust;
  return true;
}

bool PokedexCatalog::parseIndexLine(const String &line, PokedexRow &out) const {
  int p1 = line.indexOf(',');
  int p2 = line.indexOf(',', p1 + 1);
  int p3 = line.indexOf(',', p2 + 1);
  int p4 = line.indexOf(',', p3 + 1);
  int p5 = line.indexOf(',', p4 + 1);
  if (p1 < 1 || p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0) {
    return false;
  }

  copyString(out.dex, sizeof(out.dex), line.substring(0, p1));
  copyString(out.entryId, sizeof(out.entryId), line.substring(p1 + 1, p2));
  copyString(out.name, sizeof(out.name), line.substring(p2 + 1, p3));
  copyString(out.type1, sizeof(out.type1), line.substring(p3 + 1, p4));
  copyString(out.type2, sizeof(out.type2), line.substring(p4 + 1, p5));
  copyString(out.file, sizeof(out.file), line.substring(p5 + 1));
  return true;
}

bool PokedexCatalog::rowMatches(const PokedexRow &row, const String &query) const {
  String q = lowerTrimmed(query);
  if (q.length() == 0) return false;

  String name = lowerTrimmed(String(row.name));
  String entryId = lowerTrimmed(String(row.entryId));
  String type1 = lowerTrimmed(String(row.type1));
  String type2 = lowerTrimmed(String(row.type2));
  if (name.indexOf(q) >= 0 || entryId.indexOf(q) >= 0 || type1.indexOf(q) >= 0 ||
      type2.indexOf(q) >= 0) {
    return true;
  }

  return normalizedDex(String(row.dex)) == normalizedDex(query);
}

void PokedexCatalog::releaseIndex() {
  if (indexRecords_ != nullptr) {
    free(indexRecords_);
    indexRecords_ = nullptr;
  }
  if (indexNameOrder_ != nullptr) {
    free(indexNameOrder_);
    indexNameOrder_ = nullptr;
  }
  indexRows_ = 0;
}

void PokedexCatalog::buildIndex() {
#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  releaseIndex();
  if (!sdReady_ || !indexReady_) return;

  File file = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
  if (!file) {
    Logger::warn("index", "cannot open index for indexing; using linear scan");
    return;
  }

  // 2048 covers the shipped 1573-row catalog with headroom. 40 bytes per record.
  const uint16_t capacity = 2048;
  indexRecords_ = (pokedex::Record *)ps_malloc(sizeof(pokedex::Record) * capacity);
  if (indexRecords_ == nullptr) {
    file.close();
    Logger::warn("index", "PSRAM index alloc failed; using linear scan");
    return;
  }

  // The name-order array is optional: without it, A-Z ordering is unavailable
  // but dex ordering still works.
  indexNameOrder_ = (uint16_t *)ps_malloc(sizeof(uint16_t) * capacity);
  if (indexNameOrder_ == nullptr) {
    Logger::warn("index", "PSRAM name-order alloc failed; dex order only");
  }

  index_.attach(indexRecords_, capacity, indexNameOrder_);
  PokedexSdSource source(file);
  indexRows_ = index_.build(source);
  file.close();

  if (indexRows_ == 0) {
    releaseIndex();
    Logger::warn("index", "index build produced no rows; using linear scan");
    return;
  }
  Logger::info("index", String("indexed ") + indexRows_ + " rows");
#endif
}

bool PokedexCatalog::nameOrderActive() const {
  return indexRows_ > 0 && index_.nameOrderAvailable();
}

uint16_t PokedexCatalog::countMatching(const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return totalRows_;
  return index_.countMatching(filter);
}

uint32_t PokedexCatalog::ordinalOfLetter(char letter, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return 0;
  return index_.ordinalOfLetter(letter, filter);
}

bool PokedexCatalog::hasLetter(char letter, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return false;
  return index_.hasLetter(letter, filter);
}

uint32_t PokedexCatalog::ordinalOfDex(uint16_t dex, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return 0;
  return index_.ordinalOfDex(dex, filter);
}

bool PokedexCatalog::hasDexAtLeast(uint16_t dex, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return false;
  return index_.hasDexAtLeast(dex, filter);
}

uint32_t PokedexCatalog::clampOrdinal(uint32_t ordinal, const pokedex::Filter &filter) const {
  if (indexRows_ == 0) return 0;
  return index_.clampOrdinal(ordinal, filter);
}

uint8_t PokedexCatalog::window(uint32_t startOrdinal, pokedex::Order order,
                               const pokedex::Filter &filter, PokedexRow *outRows,
                               uint8_t maxRows) {
  if (indexRows_ == 0) {
    // Fallback: the pre-existing linear browse. It only understands a row start,
    // which is what an ordinal is when nothing is filtered out.
    return browse((uint16_t)startOrdinal, outRows, maxRows);
  }
#if USE_SD_POKEDEX && POKEDEX_HAS_SD_MMC
  uint16_t rowIndices[pokedex::kGridPageSize];
  const uint8_t found = index_.pageAtOrdinal(startOrdinal, order, filter, rowIndices);
  if (found == 0) return 0;

  // One open for the whole window: 18 seeks beat 18 open/close cycles.
  File file = SD_MMC.open(POKEDEX_INDEX_PATH, FILE_READ);
  if (!file) return 0;
  uint8_t written = 0;
  for (uint8_t i = 0; i < found && written < maxRows; i++) {
    if (!file.seek(index_.record(rowIndices[i]).offset)) continue;
    String line = file.readStringUntil('\n');
    if (parseIndexLine(line, outRows[written])) written++;
  }
  file.close();
  return written;
#else
  (void)order;
  (void)filter;
  (void)outRows;
  (void)maxRows;
  return 0;
#endif
}
