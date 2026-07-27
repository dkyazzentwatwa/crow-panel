#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/PokedexTypes.h"
#include "src/PokedexCatalog.h"
#include "src/PokedexDashboard.h"

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
uint16_t browseStart = 0;
uint16_t totalMatches = 0;
String activeQuery = "browse";
PokedexDetail activeDetail;
uint8_t activeDetailPage = 0;
bool detailOpen = false;

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
  dashboard.showList(rows, rowCount, selectedRow, browseStart, catalog.totalRows(), activeQuery,
                     catalog.sourceLabel(), status);
}

void printActiveRows() {
  if (rowCount == 0) {
    Serial.println(F("[pokedex] no active rows"));
    return;
  }
  catalog.printRows(Serial, rows, rowCount);
}

void loadBrowse(uint16_t start) {
  uint16_t total = catalog.totalRows();
  if (total > 0 && start >= total) {
    start = 0;
  }
  browseStart = start;
  rowCount = catalog.browse(browseStart, rows, POKEDEX_MAX_RESULTS);
  selectedRow = 0;
  activeQuery = String("browse ") + browseStart;
  totalMatches = rowCount;
  detailOpen = false;
  Serial.print(F("[pokedex] browse start="));
  Serial.print(browseStart);
  Serial.print(F(" rows="));
  Serial.println(rowCount);
  printActiveRows();
  showCurrentList(catalog.status());
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
  browseStart = 0;
  activeQuery = q;
  detailOpen = false;
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
  printSystemStatus(Serial, "pokedex-panel", eventLog.size());
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
  uint16_t start = value.length() ? (uint16_t)value.toInt() : 0;
  loadBrowse(start);
  eventLog.add(String("Browse from ") + start);
}

void cmdSearch(const String &args) {
  runSearch(args);
}

void cmdSelect(const String &args) {
  uint8_t rowIndex;
  if (!parseRowNumber(args, rowIndex)) {
    Serial.println(F("[pokedex] usage: select <row 1-8>"));
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

void handleUiEvent(const PokedexUiEvent &event) {
  if (event.type == kPokedexUiSelectRow) {
    openRow(event.row);
  } else if (event.type == kPokedexUiBackToList) {
    detailOpen = false;
    showCurrentList(catalog.status());
  } else if (event.type == kPokedexUiNextPage) {
    showDetailPage(activeDetailPage + 1);
  } else if (event.type == kPokedexUiPrevPage) {
    showDetailPage(activeDetailPage == 0 ? POKEDEX_DETAIL_PAGE_COUNT - 1 : activeDetailPage - 1);
  } else if (event.type == kPokedexUiBrowseNext) {
    uint16_t next = browseStart + POKEDEX_MAX_RESULTS;
    if (catalog.totalRows() > 0 && next >= catalog.totalRows()) {
      next = 0;
    }
    loadBrowse(next);
  } else if (event.type == kPokedexUiBrowsePrev) {
    uint16_t total = catalog.totalRows();
    uint16_t prev = 0;
    if (browseStart >= POKEDEX_MAX_RESULTS) {
      prev = browseStart - POKEDEX_MAX_RESULTS;
    } else if (total > POKEDEX_MAX_RESULTS) {
      prev = total - POKEDEX_MAX_RESULTS;
    }
    loadBrowse(prev);
  } else if (event.type == kPokedexUiOpenSearch) {
    dashboard.beginSearch(activeQuery.startsWith("browse") ? "" : activeQuery);
  } else if (event.type == kPokedexUiSearchCancel) {
    showCurrentList(catalog.status());
  } else if (event.type == kPokedexUiSearchSubmit) {
    String query = event.text;
    query.trim();
    if (query.length() > 0) {
      runSearch(query);
    } else {
      showCurrentList(catalog.status());
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
  loadBrowse(0);
  eventLog.add("Pokedex Panel booted");

  router.begin(Serial, "pokedex");
  router.on("status", "uptime, heap, flags, catalog state", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("rows", "print current result/browse rows", cmdRows);
  router.on("browse", "browse [start] - stream catalog rows", cmdBrowse);
  router.on("search", "search <name|dex|type|variant>", cmdSearch);
  router.on("select", "select <row 1-8>", cmdSelect);
  router.on("open", "open [row <n>|query] - load detail card", cmdOpen);
  router.on("page", "page [next|prev|1-5]", cmdPage);
  router.on("demo", "open a high-impact mega search card", cmdDemo);
  router.on("source", "show local source and SD layout", cmdSource);
  router.on("touch", "print last raw/mapped touch coordinates", cmdTouch);
}

void loop() {
  router.poll();

  PokedexUiEvent event;
  if (dashboard.tick(event)) {
    handleUiEvent(event);
  }

  delay(USE_DISPLAY ? 10 : 25);
}
