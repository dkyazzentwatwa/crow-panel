#ifndef POKEDEX_DASHBOARD_H
#define POKEDEX_DASHBOARD_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "PokedexTypes.h"

class PokedexDashboard {
 public:
  void begin();
  void showList(const PokedexRow *rows, uint8_t count, uint8_t selected, uint16_t browseStart,
                uint16_t totalRows, const String &query, const String &source,
                const String &status);
  void showDetail(const PokedexDetail &detail, uint8_t page, const String &source,
                  const String &status);
  bool tick(PokedexUiEvent &event);
  void requestRepaint();
  void printTouchDiagnostics(Print &out) const;

 private:
  PokedexUiMode mode_ = kPokedexUiList;
  PokedexRow rows_[POKEDEX_MAX_RESULTS];
  uint8_t rowCount_ = 0;
  uint8_t selected_ = 0;
  uint16_t browseStart_ = 0;
  uint16_t totalRows_ = 0;
  PokedexDetail detail_;
  uint8_t detailPage_ = 0;
  String query_ = "browse";
  String source_ = "mock catalog";
  String status_ = "ready";
  bool dirty_ = true;
  bool wasTouched_ = false;
  bool showTouchDot_ = false;
  uint32_t touchCount_ = 0;
  int16_t lastRawX_ = 0;
  int16_t lastRawY_ = 0;
  int16_t lastTouchX_ = 0;
  int16_t lastTouchY_ = 0;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  void draw();
  void drawList(class Arduino_GFX *g);
  void drawDetail(class Arduino_GFX *g);
  void drawHeader(class Arduino_GFX *g, const char *title);
  void drawFooter(class Arduino_GFX *g);
  int16_t calibrateX(int16_t rawX, int16_t rawY) const;
  int16_t calibrateY(int16_t rawX, int16_t rawY) const;
  bool handleTouchMapped(int16_t x, int16_t y, PokedexUiEvent &event);
  bool handleTouch(int16_t x, int16_t y, PokedexUiEvent &event);
  void drawTouchDot(class Arduino_GFX *g);
#endif
};

#endif
