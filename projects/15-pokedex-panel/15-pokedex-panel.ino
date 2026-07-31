#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/PokedexTypes.h"
#include "src/PokedexCatalog.h"
#include "src/PokedexDashboard.h"
#include "src/PokedexSprites.h"

// Project 15 - Pokedex Panel.
// Port of the local esp32-pokedex Cardputer app into a CrowPanel-first surface:
// touch + Serial navigation, SD-first catalog loading, and a mock fallback that
// keeps the demo useful when a card is absent or incomplete.
PokedexCatalog catalog;
PokedexDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;

PokedexRow rows[POKEDEX_MAX_RESULTS];
uint8_t rowCount = 0;
uint8_t selectedRow = 0;
uint16_t totalMatches = 0;
String activeQuery = "";
PokedexDetail activeDetail;
uint8_t activeDetailPage = 0;
bool detailOpen = false;

PokedexSprites sprites;
pokedex::Order browseOrder = pokedex::kOrderDex;
pokedex::Filter browseFilter;
uint32_t browseOrdinal = 0;
bool railEnabled[kRailSlotsMax] = {false};
// True while rows_[]/rowCount hold a one-off search/lookup result rather than
// the persistent grid browse window. Both render through the same grid
// (PokedexDashboard::showGrid, via showList's forwarding) so "back"/"cancel"
// needs this to know which state to restore - see showPriorContext() below.
bool showingSearchResults = false;

bool parseRowNumber(const String &args, uint8_t &rowIndex) {
  String value = args;
  value.trim();
  if (value.startsWith("row ")) {
    value.remove(0, 4);
    value.trim();
  }
  int n = value.toInt();
  if (n < 1 || n > rowCount) {
    return false;
  }
  rowIndex = (uint8_t)(n - 1);
  return true;
}

void showCurrentList(const String &status) {
  // totalMatches, not catalog.totalRows(): this path is for one-off search or
  // lookup results, and showing e.g. "of 1573" for a 3-result search would be
  // wrong. The offset argument (0) is unused by showList's forwarding.
  dashboard.showList(rows, rowCount, selectedRow, 0, totalMatches, activeQuery,
                     catalog.sourceLabel(), status);
}

void printActiveRows() {
  if (rowCount == 0) {
    Serial.println(F("[pokedex] no active rows"));
    return;
  }
  catalog.printRows(Serial, rows, rowCount);
}

// Recomputes which rail slots have rows under the active order and filter, so
// the UI can dim the ones that would jump nowhere.
void refreshRailState() {
  for (uint8_t i = 0; i < kRailSlotsMax; i++) railEnabled[i] = false;
  if (browseOrder == pokedex::kOrderName) {
    for (uint8_t i = 0; i < 26; i++) {
      railEnabled[i] = catalog.hasLetter((char)('A' + i), browseFilter);
    }
  } else {
    for (uint8_t i = 0; i < 11; i++) {
      railEnabled[i] = catalog.hasDexAtLeast((uint16_t)(i * 100), browseFilter);
    }
  }
}

// Loads the kGridPageSize-row window starting at `ordinal` in the current order
// and filter, clamped so it never runs off either end.
void loadGridWindow(uint32_t ordinal) {
  showingSearchResults = false;
  detailOpen = false;
  browseOrdinal = catalog.clampOrdinal(ordinal, browseFilter);
  rowCount = catalog.window(browseOrdinal, browseOrder, browseFilter, rows,
                            POKEDEX_MAX_RESULTS);
  selectedRow = 0;
  refreshRailState();
  dashboard.showGrid(rows, rowCount, selectedRow, browseOrdinal,
                     catalog.countMatching(browseFilter), browseOrder, browseFilter,
                     railEnabled, catalog.sourceLabel(), catalog.status());
}

void runSearch(const String &query) {
  String q = query;
  q.trim();
  if (q.length() == 0) {
    Serial.println(F("[pokedex] usage: search <name|dex|type|variant>"));
    return;
  }

  rowCount = catalog.search(q, rows, POKEDEX_MAX_RESULTS, totalMatches);
  selectedRow = 0;
  activeQuery = q;
  detailOpen = false;
  showingSearchResults = true;
  Serial.print(F("[pokedex] search=\""));
  Serial.print(q);
  Serial.print(F("\" shown="));
  Serial.print(rowCount);
  Serial.print(F(" total_matches="));
  Serial.println(totalMatches);
  printActiveRows();
  eventLog.add(String("Search ") + q + " -> " + totalMatches + " matches");
  showCurrentList(catalog.status());
}

bool openRow(uint8_t rowIndex) {
  if (rowIndex >= rowCount) {
    Serial.println(F("[pokedex] row out of range"));
    return false;
  }

  selectedRow = rowIndex;
  activeDetailPage = 0;
  if (!catalog.loadDetail(rows[rowIndex], activeDetail)) {
    Serial.println(F("[pokedex] detail load failed"));
    showCurrentList(catalog.status());
    return false;
  }

  detailOpen = true;
  Serial.print(F("[pokedex] open #"));
  Serial.print(activeDetail.dex);
  Serial.print(F(" "));
  Serial.print(activeDetail.name);
  Serial.print(F(" source="));
  Serial.println(activeDetail.fromSd ? F("sd") : F("mock"));
  Serial.print(F("[pokedex] types="));
  Serial.print(activeDetail.type1);
  if (activeDetail.type2[0] != '\0') {
    Serial.print(F("/"));
    Serial.print(activeDetail.type2);
  }
  Serial.print(F(" atk="));
  Serial.print(activeDetail.atk);
  Serial.print(F(" def="));
  Serial.print(activeDetail.def);
  Serial.print(F(" hp="));
  Serial.println(activeDetail.hp);
  eventLog.add(String("Opened ") + activeDetail.name);
  const char *detailSource = activeDetail.fromSd ? catalog.sourceLabel() : "mock fallback";
  dashboard.showDetail(activeDetail, activeDetailPage, detailSource, catalog.status());
  return true;
}

void showDetailPage(uint8_t page) {
  if (!detailOpen) {
    Serial.println(F("[pokedex] no detail open; use open <query> or tap a row"));
    return;
  }
  activeDetailPage = page % POKEDEX_DETAIL_PAGE_COUNT;
  Serial.print(F("[pokedex] page "));
  Serial.print(activeDetailPage + 1);
  Serial.print(F("/"));
  Serial.println(POKEDEX_DETAIL_PAGE_COUNT);
  const char *detailSource = activeDetail.fromSd ? catalog.sourceLabel() : "mock fallback";
  dashboard.showDetail(activeDetail, activeDetailPage, detailSource, catalog.status());
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "pokedex-panel", eventLog.size(), &router);
  Serial.print(F("[pokedex] source="));
  Serial.print(catalog.sourceLabel());
  Serial.print(F(" sd_ready="));
  Serial.print(catalog.sdReady());
  Serial.print(F(" index_ready="));
  Serial.print(catalog.indexReady());
  Serial.print(F(" total_rows="));
  Serial.println(catalog.totalRows());
  Serial.print(F("[pokedex] status="));
  Serial.println(catalog.status());
  Serial.print(F("index rows="));
  Serial.print(catalog.indexRowCount());
  Serial.print(F(" nameOrder="));
  Serial.print(catalog.nameOrderActive() ? F("yes") : F("no"));
  Serial.print(F(" order="));
  Serial.print(browseOrder == pokedex::kOrderName ? F("name") : F("dex"));
  Serial.print(F(" matching="));
  Serial.println(catalog.countMatching(browseFilter));
  sprites.printDiagnostics(Serial);
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdRows(const String &) {
  printActiveRows();
}

void cmdBrowse(const String &args) {
  String value = args;
  value.trim();
  uint32_t ordinal = value.length() ? (uint32_t)value.toInt() : browseOrdinal;
  loadGridWindow(ordinal);
  eventLog.add(String("Browse from ") + ordinal);
}

void cmdSearch(const String &args) {
  runSearch(args);
}

void cmdSelect(const String &args) {
  uint8_t rowIndex;
  if (!parseRowNumber(args, rowIndex)) {
    Serial.println(F("[pokedex] usage: select <row 1-18>"));
    return;
  }
  selectedRow = rowIndex;
  Serial.print(F("[pokedex] selected row "));
  Serial.println(selectedRow + 1);
  showCurrentList(catalog.status());
}

void cmdOpen(const String &args) {
  String value = args;
  value.trim();
  if (value.length() == 0) {
    openRow(selectedRow);
    return;
  }

  uint8_t rowIndex;
  if (value.startsWith("row ") && parseRowNumber(value, rowIndex)) {
    openRow(rowIndex);
    return;
  }

  PokedexRow row;
  if (!catalog.findFirst(value, row)) {
    Serial.print(F("[pokedex] no match for "));
    Serial.println(value);
    return;
  }
  rows[0] = row;
  rowCount = 1;
  selectedRow = 0;
  activeQuery = value;
  totalMatches = 1;
  showingSearchResults = true;
  openRow(0);
}

void cmdPage(const String &args) {
  String value = args;
  value.trim();
  value.toLowerCase();
  if (value == "prev" || value == "-") {
    showDetailPage(activeDetailPage == 0 ? POKEDEX_DETAIL_PAGE_COUNT - 1 : activeDetailPage - 1);
    return;
  }
  if (value == "next" || value == "+" || value.length() == 0) {
    showDetailPage(activeDetailPage + 1);
    return;
  }
  int page = value.toInt();
  if (page < 1 || page > POKEDEX_DETAIL_PAGE_COUNT) {
    Serial.println(F("[pokedex] usage: page [next|prev|1-5]"));
    return;
  }
  showDetailPage((uint8_t)(page - 1));
}

void cmdDemo(const String &) {
  runSearch("mega");
  if (rowCount > 0) {
    openRow(0);
  }
}

void cmdSource(const String &) {
  Serial.println(F("[pokedex] source app: /Users/cypher/Documents/GitHub/esp32-pokedex"));
  Serial.print(F("[pokedex] active source: "));
  Serial.println(catalog.sourceLabel());
  Serial.println(F("[pokedex] SD layout: /pokemon/index.csv + /pokemon/data/*.json"));
  Serial.println(F("[pokedex] SD-first default: /pokemon/index.csv + /pokemon/data/*.json"));
}

void cmdTouch(const String &) {
  dashboard.printTouchDiagnostics(Serial);
}

void cmdGrid(const String &args) {
  const uint32_t ordinal = args.length() ? (uint32_t)args.toInt() : browseOrdinal;
  loadGridWindow(ordinal);
  Serial.print(F("rows "));
  Serial.print(browseOrdinal + 1);
  Serial.print('-');
  Serial.print(browseOrdinal + rowCount);
  Serial.print(F(" of "));
  Serial.println(catalog.countMatching(browseFilter));
  catalog.printRows(Serial, rows, rowCount);
}

void cmdLetter(const String &args) {
  if (args.length() == 0) {
    Serial.println(F("usage: letter <a-z>"));
    return;
  }
  if (!catalog.nameOrderActive()) {
    Serial.println(F("name order unavailable; dex order only"));
    return;
  }
  browseOrder = pokedex::kOrderName;
  loadGridWindow(catalog.ordinalOfLetter(args[0], browseFilter));
  catalog.printRows(Serial, rows, rowCount);
}

void cmdSort(const String &args) {
  if (args == "name") {
    if (!catalog.nameOrderActive()) {
      Serial.println(F("name order unavailable; staying in dex order"));
      return;
    }
    browseOrder = pokedex::kOrderName;
  } else if (args == "dex") {
    browseOrder = pokedex::kOrderDex;
  } else {
    Serial.println(F("usage: sort [dex|name]"));
    return;
  }
  loadGridWindow(0);
  Serial.print(F("order="));
  Serial.println(browseOrder == pokedex::kOrderName ? F("name") : F("dex"));
}

void cmdFilter(const String &args) {
  if (args.length() == 0 || args == "none") {
    browseFilter.type1 = pokedex::kTypeAny;
  } else {
    char wanted[16];
    snprintf(wanted, sizeof(wanted), "%s", args.c_str());
    if (wanted[0] >= 'a' && wanted[0] <= 'z') wanted[0] = (char)(wanted[0] - 'a' + 'A');
    const uint8_t id = pokedex::typeIdFromName(wanted);
    if (id == pokedex::kTypeAny) {
      Serial.println(F("unknown type"));
      return;
    }
    browseFilter.type1 = id;
  }
  loadGridWindow(0);
  Serial.print(F("matching="));
  Serial.println(catalog.countMatching(browseFilter));
}

void cmdShadows(const String &args) {
  if (args == "on") {
    browseFilter.showShadows = true;
  } else if (args == "off") {
    browseFilter.showShadows = false;
  } else {
    browseFilter.showShadows = !browseFilter.showShadows;
  }
  loadGridWindow(0);
  Serial.print(F("shadows="));
  Serial.println(browseFilter.showShadows ? F("on") : F("off"));
}

void cmdSprite(const String &args) {
  if (args.length() == 0) {
    sprites.printDiagnostics(Serial);
    return;
  }
  uint16_t key = 0;
  const uint16_t *pixels = sprites.tile(args.c_str(), &key);
  Serial.print(args);
  Serial.print(pixels != nullptr ? F(" decoded in ") : F(" failed after "));
  Serial.print(sprites.lastDecodeMicros());
  Serial.print(F(" us key=0x"));
  Serial.println(key, HEX);
}

// "Back" from detail, or cancelling/clearing search, returns to whichever
// context was active: the persistent grid browse window, or a one-off
// search/lookup result. Both render through the same grid, so without this
// the wrong one's chrome (offset, order, filter, rail) would silently
// overwrite the other's - see the design note at the top of this task.
void showPriorContext() {
  if (showingSearchResults) {
    detailOpen = false;
    showCurrentList(catalog.status());
  } else {
    loadGridWindow(browseOrdinal);
  }
}

void handleUiEvent(const PokedexUiEvent &event) {
  if (event.type == kPokedexUiSelectRow) {
    openRow(event.row);
  } else if (event.type == kPokedexUiBackToList) {
    showPriorContext();
  } else if (event.type == kPokedexUiNextPage) {
    showDetailPage(activeDetailPage + 1);
  } else if (event.type == kPokedexUiPrevPage) {
    showDetailPage(activeDetailPage == 0 ? POKEDEX_DETAIL_PAGE_COUNT - 1 : activeDetailPage - 1);
  } else if (event.type == kPokedexUiSelectTab) {
    showDetailPage(event.tab);
  } else if (event.type == kPokedexUiJumpTop) {
    loadGridWindow(0);
  } else if (event.type == kPokedexUiJumpLetter) {
    loadGridWindow(catalog.ordinalOfLetter(event.letter, browseFilter));
  } else if (event.type == kPokedexUiJumpDex) {
    loadGridWindow(catalog.ordinalOfDex(event.dex, browseFilter));
  } else if (event.type == kPokedexUiToggleSort) {
    // A-Z is only meaningful when the name-order buffer was allocated.
    if (browseOrder == pokedex::kOrderDex && catalog.nameOrderActive()) {
      browseOrder = pokedex::kOrderName;
    } else {
      browseOrder = pokedex::kOrderDex;
    }
    loadGridWindow(0);
  } else if (event.type == kPokedexUiToggleShadows) {
    browseFilter.showShadows = !browseFilter.showShadows;
    loadGridWindow(0);
  } else if (event.type == kPokedexUiCycleType) {
    if (browseFilter.type1 == pokedex::kTypeAny) {
      browseFilter.type1 = 0;
    } else if (browseFilter.type1 + 1 >= pokedex::kTypeCount) {
      browseFilter.type1 = pokedex::kTypeAny;
    } else {
      browseFilter.type1++;
    }
    loadGridWindow(0);
  } else if (event.type == kPokedexUiBrowseNext) {
    loadGridWindow(browseOrdinal + pokedex::kGridPageSize);
  } else if (event.type == kPokedexUiBrowsePrev) {
    // browseOrdinal is unsigned: guard against underflow rather than
    // subtracting past 0.
    loadGridWindow(browseOrdinal < pokedex::kGridPageSize
                       ? 0
                       : browseOrdinal - pokedex::kGridPageSize);
  } else if (event.type == kPokedexUiOpenSearch) {
    dashboard.beginSearch(activeQuery);
  } else if (event.type == kPokedexUiSearchCancel) {
    showPriorContext();
  } else if (event.type == kPokedexUiSearchSubmit) {
    String query = event.text;
    query.trim();
    if (query.length() > 0) {
      runSearch(query);
    } else {
      showPriorContext();
    }
  }
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Pokedex Panel");
  printHardwareProfile(Serial, activeHardwareProfile());

  storage.begin("pokedex");
  // Mount SD before bringing up the DSI display, matching every other SD+
  // display project in this repo (Cypher Tune MPC's LoopLibrary::begin(),
  // Cypher Desk's DeskStorage::begin(), Cypher Boy's GbRomStore::begin() all
  // run before their display init). Boot-probe instrumentation isolated a
  // real black-screen bug to the reverse order: once the DSI panel claims
  // whatever DMA/GPDMA resource the native SDMMC host also needs, SD_MMC's
  // mount call in catalog.begin() never returns.
  catalog.begin();
  dashboard.begin();
  sprites.begin();
  dashboard.attachSprites(&sprites);
  loadGridWindow(0);
  eventLog.add("Pokedex Panel booted");

  router.begin(Serial, "pokedex");
  router.on("status", "uptime, heap, flags, catalog state", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("rows", "print current result/browse rows", cmdRows);
  router.on("browse", "browse [ordinal] - jump the grid window", cmdBrowse);
  router.on("search", "search <name|dex|type|variant>", cmdSearch);
  router.on("select", "select <row 1-18>", cmdSelect);
  router.on("open", "open [row <n>|query] - load detail card", cmdOpen);
  router.on("page", "page [next|prev|1-5]", cmdPage);
  router.on("demo", "open a high-impact mega search card", cmdDemo);
  router.on("source", "show local source and SD layout", cmdSource);
  router.on("touch", "print last raw/mapped touch coordinates", cmdTouch);
  router.on("grid", "grid [ordinal] - show a grid window", cmdGrid);
  router.on("letter", "letter <a-z> - jump in A-Z order", cmdLetter);
  router.on("sort", "sort [dex|name] - set browse order", cmdSort);
  router.on("filter", "filter [type|none] - set the type filter", cmdFilter);
  router.on("shadows", "shadows [on|off] - toggle shadow forms", cmdShadows);
  router.on("sprite", "sprite [entry_id] - decode report", cmdSprite);
}

void loop() {
  router.poll();

  PokedexUiEvent event;
  if (dashboard.tick(event)) {
    handleUiEvent(event);
  }

  delay(USE_DISPLAY ? 10 : 25);
}
