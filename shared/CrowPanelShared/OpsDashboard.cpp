#include "OpsDashboard.h"
#include "DisplayBringup.h"
#include "HardwareProfile.h"
#include "Logger.h"
#include "Throttle.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#include "DashboardWidgets.h"
#endif

namespace {
String fitForSerial(const String &s, size_t maxLen) {
  if (s.length() <= maxLen) return s;
  String out = s.substring(0, maxLen > 3 ? maxLen - 3 : maxLen);
  out += "...";
  return out;
}
}

void OpsDashboard::begin(const char *title, const char *subtitle, const char *linkLabel) {
  title_ = title ? title : "OPS";
  subtitle_ = subtitle ? subtitle : "";
  linkLabel_ = linkLabel ? linkLabel : "MOCK";
  banner_ = "mock-first console";
  detailTitle_ = "Ready";
  detailBody_ = "Use Serial commands or touch a tile.";
  footer_ = "scaffolded";
  ready_ = false;
  dirty_ = true;
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  ready_ = CrowDisplay::begin(activeHardwareProfile(), title_.c_str()) &&
           (CrowDisplay::canvas() != nullptr);
  if (ready_) {
    drawChrome();
    repaint();
  }
#endif
  Logger::info("ui", String("ops-dashboard title=") + title_ + " link=" + linkLabel_);
}

void OpsDashboard::tick() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return;
  int16_t tx, ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  if (touched && !wasTouched_) {
    const int16_t x = 16;
    const int16_t w = 336;
    const int16_t top = 112;
    const int16_t h = 48;
    const int16_t gap = 8;
    for (uint8_t i = 0; i < kMaxTiles; i++) {
      int16_t y = top + i * (h + gap);
      if (tiles_[i].active && tx >= x && tx <= x + w && ty >= y && ty <= y + h) {
        selected_ = i;
        dirty_ = true;
        Logger::info("touch", String("selected tile ") + i + " " + tiles_[i].title);
        break;
      }
    }
  }
  wasTouched_ = touched;

  if (dirty_) {
    repaint();
    dirty_ = false;
    return;
  }

  static Throttle refresh(1000);
  if (refresh.ready()) {
    drawHeader();
    drawFooter();
  }
#endif
}

void OpsDashboard::setTile(uint8_t index, const String &title, const String &value,
                           const String &meta, bool active) {
  if (index >= kMaxTiles) return;
  tiles_[index].title = title;
  tiles_[index].value = value;
  tiles_[index].meta = meta;
  tiles_[index].active = active;
  if (selected_ < 0 && active) selected_ = index;
  markDirty();
}

void OpsDashboard::clearTile(uint8_t index) {
  if (index >= kMaxTiles) return;
  tiles_[index] = Tile();
  if (selected_ == (int8_t)index) selected_ = -1;
  markDirty();
}

void OpsDashboard::select(uint8_t index) {
  if (index >= kMaxTiles || !tiles_[index].active) return;
  selected_ = index;
  markDirty();
}

int8_t OpsDashboard::selectedIndex() const {
  return selected_;
}

void OpsDashboard::setBanner(const String &text) {
  banner_ = fitForSerial(text, 96);
  markDirty();
}

void OpsDashboard::setDetail(const String &title, const String &body) {
  detailTitle_ = fitForSerial(title, 64);
  detailBody_ = fitForSerial(body, 220);
  markDirty();
}

void OpsDashboard::setFooter(const String &text) {
  footer_ = fitForSerial(text, 120);
  markDirty();
}

void OpsDashboard::repaint() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return;
  drawHeader();
  drawTiles();
  drawBanner();
  drawDetail();
  drawFooter();
#endif
}

void OpsDashboard::markDirty() {
  dirty_ = true;
}

void OpsDashboard::drawChrome() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  g->fillRect(0, 0, 1024, 72, kSurface);
  g->fillRect(0, 70, 1024, 2, kAccent);
  towerIcon(g, 18, 20, kAccent);
  text(g, 58, 12, title_.c_str(), fontL(), kTextHi, kLeft);
  text(g, 58, 42, subtitle_.c_str(), fontS(), kTextMut, kLeft);
  text(g, 20, 86, "SURFACES", fontS(), kTextMut, kLeft);
#endif
}

void OpsDashboard::drawHeader() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(500, 0, 524, 70, kSurface);
  char heap[20];
  snprintf(heap, sizeof(heap), "%luK", (unsigned long)(ESP.getFreeHeap() / 1024));
  char up[24];
  snprintf(up, sizeof(up), "UP %lus", (unsigned long)(millis() / 1000));
  int16_t x = 1008;
  x -= pill(g, x - (int16_t)textWidth(g, up, fontS()) - 24, 22, up, fontS(), kTextHi, kSurfaceHi);
  x -= 10;
  x -= pill(g, x - (int16_t)textWidth(g, heap, fontS()) - 24, 22, heap, fontS(), kTextHi, kSurfaceHi);
  x -= 10;
  pill(g, x - (int16_t)textWidth(g, linkLabel_.c_str(), fontS()) - 24, 22,
       linkLabel_.c_str(), fontS(), kBg, kAccent);
#endif
}

void OpsDashboard::drawTiles() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t x = 16;
  const int16_t w = 336;
  const int16_t top = 112;
  const int16_t h = 48;
  const int16_t gap = 8;
  g->fillRect(0, 104, 368, 436, kBg);
  for (uint8_t i = 0; i < kMaxTiles; i++) {
    if (!tiles_[i].active) continue;
    int16_t y = top + i * (h + gap);
    bool selected = selected_ == (int8_t)i;
    panel(g, x, y, w, h, 8, selected ? kSurfaceHi : kSurface,
          selected ? 2 : 1, selected ? kAccent : kLine);
    text(g, x + 14, y + 8, tiles_[i].title.c_str(), fontS(),
         selected ? kTextHi : kTextMut, kLeft);
    text(g, x + w - 14, y + 8, tiles_[i].value.c_str(), fontS(),
         selected ? kAccent : kTextHi, kRight);
    text(g, x + 14, y + 28, tiles_[i].meta.c_str(), fontS(), kTextMut, kLeft);
  }
#endif
}

void OpsDashboard::drawBanner() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(372, 88, 636, 48, kBg);
  panel(g, 372, 88, 636, 44, 8, kSurface, 1, kLine);
  statusDot(g, 396, 110, 7, kAccent);
  text(g, 414, 99, banner_.c_str(), fontS(), kTextHi, kLeft);
#endif
}

void OpsDashboard::drawDetail() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(372, 152, 636, 368, kBg);
  panel(g, 372, 152, 636, 368, 8, kSurface, 1, kLine);
  text(g, 396, 180, detailTitle_.c_str(), fontL(), kTextHi, kLeft);
  String body = detailBody_;
  const int16_t x = 396;
  int16_t y = 232;
  while (body.length() > 0 && y < 488) {
    int cut = body.indexOf('|');
    String line = cut >= 0 ? body.substring(0, cut) : body;
    body = cut >= 0 ? body.substring(cut + 1) : "";
    line.trim();
    text(g, x, y, line.c_str(), fontM(), kTextMut, kLeft);
    y += 34;
  }
#endif
}

void OpsDashboard::drawFooter() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  using namespace Widgets;
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, 540, 1024, 60, kSurface);
  text(g, 18, 562, footer_.c_str(), fontS(), kTextMut, kLeft);
  text(g, 1006, 562, "compile-ready only until flashed", fontS(), kTextMut, kRight);
#endif
}
