#include "WireTapDashboard.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

namespace {
String fitForDisplay(const String &s, size_t maxLen) {
  if (s.length() <= maxLen) return s;
  String out = s.substring(0, maxLen > 3 ? maxLen - 3 : maxLen);
  out += "...";
  return out;
}

const char *commandForTile(uint8_t index) {
  switch (index) {
    case 0: return "mode <hiz|i2c|spi|uart|gpio>";
    case 1: return "pins";
    case 2: return "i2c scan";
    case 3: return "spi id";
    case 4: return "uart rx";
    case 5: return "gpio get <pin>";
    default: return "help";
  }
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
using namespace Widgets;

constexpr int16_t kScreenW = 1024;
constexpr int16_t kHeaderH = 74;
constexpr int16_t kLaneX = 18;
constexpr int16_t kLaneW = 332;
constexpr int16_t kLaneTop = 112;
constexpr int16_t kLaneH = 60;
constexpr int16_t kLaneGap = 8;
constexpr int16_t kMainX = 372;
constexpr int16_t kMainW = 634;
constexpr int16_t kBannerY = 88;
constexpr int16_t kFooterY = 540;
constexpr int16_t kFooterH = 60;

String fitText(Arduino_GFX *g, const String &s, const GFXfont *font, int16_t maxW) {
  if (textWidth(g, s.c_str(), font) <= maxW) return s;
  String t = s;
  while (t.length() > 1 && textWidth(g, (t + "...").c_str(), font) > maxW) {
    t.remove(t.length() - 1);
  }
  return t + "...";
}

int16_t laneY(uint8_t index) { return kLaneTop + index * (kLaneH + kLaneGap); }

uint16_t tileAccent(const String &valueText, const String &metaText, bool active, uint8_t index) {
  if (!active) return kLine;
  String meta = metaText;
  meta.toLowerCase();
  String value = valueText;
  value.toLowerCase();
  if (index == 3 && (meta.indexOf("clocked") >= 0 || value.indexOf("jedec") >= 0)) {
    return meta.indexOf("disabled") >= 0 ? kGreen : kAmber;
  }
  if (meta.indexOf("mock") >= 0) return kAccent;
  if (meta.indexOf("rx only") >= 0 || meta.indexOf("high-z") >= 0 ||
      meta.indexOf("address only") >= 0 || meta.indexOf("3.3v") >= 0) {
    return kGreen;
  }
  return kAccent;
}

void drawSafetyLine(Arduino_GFX *g, int16_t x, int16_t y, const char *label,
                    const String &value, uint16_t color) {
  statusDot(g, x + 8, y + 10, 5, color);
  text(g, x + 24, y, label, fontS(), kTextMut, kLeft);
  text(g, x + 216, y, fitText(g, value, fontS(), 118).c_str(), fontS(), kTextHi, kRight);
}
#endif
}  // namespace

void WireTapDashboard::begin(const char *title, const char *subtitle, const char *linkLabel) {
  title_ = title ? title : "WIRETAP";
  subtitle_ = subtitle ? subtitle : "BENCHOPS CONSOLE";
  linkLabel_ = linkLabel ? linkLabel : "MOCK";
  banner_ = "safe bench console ready";
  detailTitle_ = "No-Drive Defaults";
  detailBody_ = "GPIO reads use INPUT/high-Z|I2C scans addresses only|UART is RX-only|SPI ID clocking is opt-in";
  footer_ = "WireTap BenchOps is compile-ready only until real wiring proof is captured";
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
  Logger::info("ui", String("wiretap-dashboard title=") + title_ + " link=" + linkLabel_);
}

void WireTapDashboard::tick() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return;
  int16_t tx, ty;
  bool touched = CrowDisplay::touchPoint(tx, ty);
  if (touched && !wasTouched_) {
    for (uint8_t i = 0; i < kMaxTiles; i++) {
      int16_t y = laneY(i);
      if (tiles_[i].active && tx >= kLaneX && tx <= kLaneX + kLaneW &&
          ty >= y && ty <= y + kLaneH) {
        selectFromTouch(i);
        Logger::info("touch", String("selected wiretap lane ") + i + " " + tiles_[i].title);
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

void WireTapDashboard::setTile(uint8_t index, const String &title, const String &value,
                               const String &meta, bool active) {
  if (index >= kMaxTiles) return;
  tiles_[index].title = title;
  tiles_[index].value = value;
  tiles_[index].meta = meta;
  tiles_[index].active = active;
  if (selected_ < 0 && active) selected_ = index;
  markDirty();
}

void WireTapDashboard::clearTile(uint8_t index) {
  if (index >= kMaxTiles) return;
  tiles_[index] = Tile();
  if (selected_ == (int8_t)index) selected_ = -1;
  markDirty();
}

void WireTapDashboard::select(uint8_t index) {
  if (index >= kMaxTiles || !tiles_[index].active) return;
  selected_ = index;
  markDirty();
}

int8_t WireTapDashboard::selectedIndex() const { return selected_; }

void WireTapDashboard::setBanner(const String &text) {
  banner_ = fitForDisplay(text, 96);
  markDirty();
}

void WireTapDashboard::setDetail(const String &title, const String &body) {
  detailTitle_ = fitForDisplay(title, 64);
  detailBody_ = fitForDisplay(body, 260);
  markDirty();
}

void WireTapDashboard::setFooter(const String &text) {
  footer_ = fitForDisplay(text, 130);
  markDirty();
}

void WireTapDashboard::repaint() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (!ready_) return;
  drawHeader();
  drawProtocolLanes();
  drawBanner();
  drawSafetyPanel();
  drawDetail();
  drawFooter();
#endif
}

void WireTapDashboard::markDirty() { dirty_ = true; }

void WireTapDashboard::selectFromTouch(uint8_t index) {
  if (index >= kMaxTiles || !tiles_[index].active) return;
  selected_ = index;
  detailTitle_ = tiles_[index].title + " Lane";
  detailBody_ = tiles_[index].value + "|" + tiles_[index].meta +
                "|Serial: " + commandForTile(index);
  markDirty();
}

void WireTapDashboard::drawChrome() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillScreen(kBg);
  g->fillRect(0, 0, kScreenW, kHeaderH, kSurface);
  g->fillRect(0, kHeaderH - 2, kScreenW, 2, kAccent);
  towerIcon(g, 18, 20, kAccent);
  text(g, 58, 12, title_.c_str(), fontL(), kTextHi, kLeft);
  text(g, 58, 42, subtitle_.c_str(), fontS(), kTextMut, kLeft);
  text(g, kLaneX + 2, 88, "PROBE LANES", fontS(), kTextMut, kLeft);
  text(g, kMainX, 88, "BENCH STATE", fontS(), kTextMut, kLeft);
#endif
}

void WireTapDashboard::drawHeader() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(456, 0, kScreenW - 456, kHeaderH - 3, kSurface);

  char heap[20];
  snprintf(heap, sizeof(heap), "%luK", (unsigned long)(ESP.getFreeHeap() / 1024));
  char up[24];
  snprintf(up, sizeof(up), "UP %lus", (unsigned long)(millis() / 1000));

  int16_t x = 1006;
  x -= pill(g, x - (int16_t)textWidth(g, up, fontS()) - 24, 22,
            up, fontS(), kTextHi, kSurfaceHi);
  x -= 10;
  x -= pill(g, x - (int16_t)textWidth(g, heap, fontS()) - 24, 22,
            heap, fontS(), kTextHi, kSurfaceHi);
  x -= 10;
  pill(g, x - (int16_t)textWidth(g, linkLabel_.c_str(), fontS()) - 24, 22,
       linkLabel_.c_str(), fontS(), kBg, kAccent);

  const char *mode = tiles_[0].active ? tiles_[0].value.c_str() : "hiz";
  pill(g, 456, 22, mode, fontS(), kTextHi, kSurfaceHi);
  pill(g, 530, 22, "NO DEFAULT DRIVE", fontS(), kBg, kGreen);
#endif
}

void WireTapDashboard::drawProtocolLanes() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, 104, 366, 430, kBg);
  for (uint8_t i = 0; i < kMaxTiles; i++) {
    if (!tiles_[i].active) continue;
    int16_t y = laneY(i);
    if (y + kLaneH > kFooterY - 4) continue;
    bool selected = selected_ == (int8_t)i;
    uint16_t accent = tileAccent(tiles_[i].value, tiles_[i].meta, tiles_[i].active, i);
    panel(g, kLaneX, y, kLaneW, kLaneH, 8, selected ? kSurfaceHi : kSurface,
          selected ? 2 : 1, selected ? accent : kLine);
    g->fillRoundRect(kLaneX + 10, y + 12, 6, kLaneH - 24, 3, accent);
    text(g, kLaneX + 26, y + 9,
         fitText(g, tiles_[i].title, fontS(), 112).c_str(),
         fontS(), selected ? kTextHi : kTextMut, kLeft);
    text(g, kLaneX + kLaneW - 14, y + 9,
         fitText(g, tiles_[i].value, fontS(), 124).c_str(),
         fontS(), accent, kRight);
    text(g, kLaneX + 26, y + 33,
         fitText(g, tiles_[i].meta, fontS(), kLaneW - 52).c_str(),
         fontS(), kTextMut, kLeft);
  }
#endif
}

void WireTapDashboard::drawBanner() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(kMainX, kBannerY, kMainW, 48, kBg);
  panel(g, kMainX, kBannerY, kMainW, 44, 8, kSurface, 1, kLine);
  statusDot(g, kMainX + 24, kBannerY + 22, 7, kGreen);
  text(g, kMainX + 44, kBannerY + 12,
       fitText(g, banner_, fontS(), kMainW - 72).c_str(), fontS(), kTextHi, kLeft);
#endif
}

void WireTapDashboard::drawSafetyPanel() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t y = 150;
  g->fillRect(kMainX, y, kMainW, 124, kBg);

  panel(g, kMainX, y, 200, 116, 8, kSurface, 1, kLine);
  text(g, kMainX + 16, y + 12, "SAFETY GATE", fontS(), kTextMut, kLeft);
  text(g, kMainX + 16, y + 40, "READ-FIRST", fontL(), kTextHi, kLeft);
  text(g, kMainX + 16, y + 76, "3.3V TTL targets", fontS(), kGreen, kLeft);

  panel(g, kMainX + 216, y, 200, 116, 8, kSurface, 1, kLine);
  text(g, kMainX + 232, y + 12, "DEFAULT DRIVE", fontS(), kTextMut, kLeft);
  text(g, kMainX + 232, y + 40, "NONE", fontL(), kTextHi, kLeft);
  hBar(g, kMainX + 232, y + 80, 152, 10, 1.0f, kGreen, kLine);

  panel(g, kMainX + 432, y, 202, 116, 8, kSurface, 1, kLine);
  text(g, kMainX + 448, y + 12, "SPI CLOCKING", fontS(), kTextMut, kLeft);
  String spi = tiles_[3].active ? tiles_[3].meta : "disabled by default";
  bool spiClocked = spi.indexOf("clocked") >= 0;
  text(g, kMainX + 448, y + 40, spiClocked ? "OPT-IN" : "BLOCKED", fontL(),
       spiClocked ? kAmber : kGreen, kLeft);
  text(g, kMainX + 448, y + 76,
       fitText(g, spi, fontS(), 168).c_str(), fontS(), kTextMut, kLeft);

  const int16_t railY = y + 132;
  g->fillRect(kMainX, railY - 4, kMainW, 64, kBg);
  panel(g, kMainX, railY, kMainW, 58, 8, kSurface, 1, kLine);
  drawSafetyLine(g, kMainX + 14, railY + 15, "GPIO", "INPUT high-Z", kGreen);
  drawSafetyLine(g, kMainX + 230, railY + 15, "I2C", "address only", kGreen);
  drawSafetyLine(g, kMainX + 446, railY + 15, "UART", "RX only", kGreen);
#endif
}

void WireTapDashboard::drawDetail() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  const int16_t y = 344;
  g->fillRect(kMainX, y, kMainW, 186, kBg);
  panel(g, kMainX, y, kMainW, 186, 8, kSurface, 1, kLine);
  text(g, kMainX + 20, y + 18,
       fitText(g, detailTitle_, fontL(), kMainW - 40).c_str(),
       fontL(), kTextHi, kLeft);

  String body = detailBody_;
  int16_t lineY = y + 68;
  while (body.length() > 0 && lineY < y + 166) {
    int cut = body.indexOf('|');
    String line = cut >= 0 ? body.substring(0, cut) : body;
    body = cut >= 0 ? body.substring(cut + 1) : "";
    line.trim();
    if (line.length() == 0) continue;
    statusDot(g, kMainX + 26, lineY + 8, 4, kAccent);
    text(g, kMainX + 42, lineY,
         fitText(g, line, fontM(), kMainW - 72).c_str(),
         fontM(), kTextMut, kLeft);
    lineY += 30;
  }
#endif
}

void WireTapDashboard::drawFooter() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g) return;
  g->fillRect(0, kFooterY, kScreenW, kFooterH, kSurface);
  text(g, 18, kFooterY + 22,
       fitText(g, footer_, fontS(), 680).c_str(),
       fontS(), kTextMut, kLeft);
  text(g, 1006, kFooterY + 22, "Serial is source of truth", fontS(), kTextMut, kRight);
#endif
}
