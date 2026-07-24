#include "PipBoyTerminal.h"

#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#include <time.h>

namespace {
constexpr uint16_t kBg = 0x0000;
constexpr uint16_t kInk = 0xBDF7;
constexpr uint16_t kDim = 0x4A69;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kBright = 0x9FF3;
constexpr uint16_t kAlert = 0xFD20;
constexpr uint16_t kPanel = 0x10A2;
const char *kPageNames[] = {"HOME", "STAT", "MAP", "ITEMS", "DATA", "RADIO"};
const char *kItems[] = {"STIMPAK", "WATER PURIFIER", "VAULT SUIT", "PLASMA PISTOL"};
const char *kItemNotes[] = {"Restores a little courage after an unexpectedly long day.",
                            "Clean enough for one more stretch through the wastes.",
                            "Standard issue. Slightly more heroic than practical.",
                            "Calibrated for theatrical effect. Safety remains on."};
const char *kBootSteps[] = {"CHECKING DISPLAY BUS", "MOUNTING LOCAL ARCHIVE", "CALIBRATING TOUCH GRID",
                            "LINKING C6 WEATHER RELAY", "LOADING VAULT PROFILE", "SYSTEM READY"};
bool inside(int16_t x, int16_t y, int16_t left, int16_t top, int16_t w, int16_t h) {
  return x >= left && x < left + w && y >= top && y < top + h;
}
void text(Arduino_GFX *g, int16_t x, int16_t y, const String &value, uint16_t color, uint8_t size = 1) {
  g->setTextSize(size); g->setTextColor(color, kBg); g->setCursor(x, y); g->print(value);
}
void box(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h, bool selected = false) {
  g->drawRect(x, y, w, h, selected ? kBright : kDim);
  if (selected) g->drawRect(x + 2, y + 2, w - 4, h - 4, kGreen);
}
String clockText() {
  time_t now = time(nullptr); struct tm info = {};
  if (now > 1600000000L && localtime_r(&now, &info)) {
    char out[18]; strftime(out, sizeof(out), "%H:%M:%S", &info); return String(out);
  }
  uint32_t seconds = millis() / 1000UL;
  char out[18]; snprintf(out, sizeof(out), "%02lu:%02lu:%02lu", seconds / 3600UL,
                         (seconds / 60UL) % 60UL, seconds % 60UL); return String(out);
}
String tempText(float value) { return isnan(value) ? "--" : String(value, 0) + "C"; }
}

void PipBoyTerminal::begin() {
  // Match the device-proven Cypher Desk bring-up order: mount SD before the
  // MIPI-DSI framebuffer starts. Initializing SD_MMC after DSI is active can
  // leave the panel backlit but blank on this CrowPanel.
  media_.begin();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::begin(activeHardwareProfile(), "PIP-BOY 3000");
  splashActive_ = true;
  splashStartedMs_ = millis();
  splashStep_ = 0;
  drawSplash_(CrowDisplay::canvas());
#endif
#if USE_WIFI
  setenv("TZ", PIPBOY_TZ, 1);
  tzset();
#endif
  network_.begin("https://api.open-meteo.com", PIPBOY_WIFI_SSID, PIPBOY_WIFI_PASSWORD);
  world_.begin(PIPBOY_WEATHER_LAT, PIPBOY_WEATHER_LON, PIPBOY_WEATHER_LABEL, 6);
  Logger::info("pipboy", "terminal initialized");
}

void PipBoyTerminal::tick() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  CrowDisplay::tick();
  if (splashActive_) {
    tickSplash_(CrowDisplay::canvas());
    if (millis() - splashStartedMs_ < 3200) return;
    splashActive_ = false;
    dirty_ = true;
  }
#endif
  network_.maintain();
  WorldFeeds updated;
  if (network_.connected() && world_.poll(updated)) { feeds_ = updated; dirty_ = true; }
  media_.tick();
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  int16_t x = 0, y = 0; bool touched = CrowDisplay::touchPoint(x, y);
  if (touched && !wasTouched_ && millis() - lastTouchMs_ > 130) {
    lastTouchX_ = x; lastTouchY_ = y; touchCount_++; lastTouchMs_ = millis(); handleTouch_(x, y);
  }
  wasTouched_ = touched;
  // A full frame clears the directly scanned DSI panel before redrawing it.
  // Keeping static pages dirty-only avoids the visible 350 ms flash that a
  // periodic full repaint produced on the real CrowPanel.
  if (dirty_) {
    draw_();
  } else {
    Arduino_GFX *g = CrowDisplay::canvas();
    if (page_ == kPipStat && millis() - lastDynamicDrawMs_ >= 1000) drawStatClock_(g);
    tickAnimation_(g);
  }
#endif
}

void PipBoyTerminal::page(PipBoyPage page) {
  page_ = page;
  tabPulseStartedMs_ = millis();
  dirty_ = true;
}
void PipBoyTerminal::nextTrack() {
  if (!media_.trackCount()) { media_.speakerTest(); dirty_ = true; return; }
  uint8_t next = media_.playing() ? (media_.activeTrack() + 1) % media_.trackCount() : 0;
  media_.playTrack(next); dirty_ = true;
}

void PipBoyTerminal::handleTouch_(int16_t x, int16_t y) {
  if (y < 66) { page(static_cast<PipBoyPage>(min<uint8_t>(5, x / 171))); return; }
  if (page_ == kPipHome) {
    uint8_t col = x / 330, row = (y - 110) / 190;
    if (col < 3 && row < 2) page(static_cast<PipBoyPage>(1 + row * 3 + col));
  } else if (page_ == kPipItems && y > 120 && y < 390) {
    selectedItem_ = min<uint8_t>(3, (y - 130) / 62); dirty_ = true;
  } else if (page_ == kPipData && y > 130 && y < 440) {
    if (media_.imageCount()) selectedImage_ = (selectedImage_ + 1) % media_.imageCount();
    dirty_ = true;
  } else if (page_ == kPipRadio && y > 410) {
    if (x < 330) { if (media_.playing()) media_.stop(); else nextTrack(); }
    else if (x < 660) nextTrack(); else media_.setVolume(media_.volume() >= 90 ? 20 : media_.volume() + 10);
    dirty_ = true;
  } else if (page_ == kPipStat && y > 430) { world_.refresh(feeds_, "weather"); dirty_ = true; }
}

void PipBoyTerminal::drawHeader_(Arduino_GFX *g) {
  g->fillScreen(kBg);
  for (int16_t y = 0; y < 600; y += 5) g->drawFastHLine(0, y, 1024, 0x0841);
  for (uint8_t i = 0; i < 6; ++i) {
    int16_t x = i * 171; g->drawRect(x, 0, 170, 64, page_ == i ? kBright : kDim);
    if (page_ == i) g->fillRect(x + 2, 2, 166, 60, kPanel);
    text(g, x + 46, 25, kPageNames[i], page_ == i ? kBright : kInk, 2);
  }
  text(g, 18, 575, "PIP-BOY 3000 // CROWPANEL FIELD TERMINAL", kDim);
  text(g, 785, 575, media_.sdReady() ? "SD ONLINE" : "SD DEMO", media_.sdReady() ? kGreen : kAlert);
}

void PipBoyTerminal::drawSplash_(Arduino_GFX *g) {
  if (g == nullptr) return;
  g->fillScreen(kBg);
  for (int16_t y = 0; y < 600; y += 5) g->drawFastHLine(0, y, 1024, 0x0841);

  const int16_t cx = 512;
  const int16_t cy = 242;
  g->drawCircle(cx, cy, 126, kDim);
  g->drawCircle(cx, cy, 118, kDim);
  g->drawCircle(cx, cy, 90, kDim);
  g->drawCircle(cx, cy, 18, kBright);
  for (uint8_t i = 0; i < 12; ++i) {
    float angle = i * PI / 6.0f;
    int16_t x1 = cx + cosf(angle) * 96;
    int16_t y1 = cy + sinf(angle) * 96;
    int16_t x2 = cx + cosf(angle) * 116;
    int16_t y2 = cy + sinf(angle) * 116;
    g->drawLine(x1, y1, x2, y2, kDim);
  }
  g->fillCircle(cx, cy, 10, kGreen);
  text(g, 356, 398, "PIP-BOY 3000", kBright, 4);
  text(g, 330, 450, "PERSONAL INFORMATION PROCESSOR", kInk, 2);
  text(g, 365, 503, "BOOT SEQUENCE STANDBY", kGreen, 2);
  g->drawRect(282, 536, 460, 14, kDim);
  g->fillRect(286, 540, 1, 6, kGreen);
  text(g, 447, 564, "CROWPANEL EDITION", kDim);
}

void PipBoyTerminal::tickSplash_(Arduino_GFX *g) {
  if (g == nullptr) return;
  const uint32_t elapsed = millis() - splashStartedMs_;
  if (elapsed >= 3100 && splashStep_ == 6) return;
  if (elapsed < 120 || elapsed / 110 == splashStep_) return;

  const int16_t cx = 512;
  const int16_t cy = 242;
  uint8_t segments = min<uint8_t>(12, elapsed / 150);
  for (uint8_t i = 0; i < 12; ++i) {
    float angle = i * PI / 6.0f;
    int16_t x1 = cx + cosf(angle) * 96;
    int16_t y1 = cy + sinf(angle) * 96;
    int16_t x2 = cx + cosf(angle) * 116;
    int16_t y2 = cy + sinf(angle) * 116;
    g->drawLine(x1, y1, x2, y2, i < segments ? kGreen : kDim);
  }
  g->drawCircle(cx, cy, 118, segments == 12 ? kGreen : kDim);
  int16_t scanY = cy - 70 + ((elapsed / 28) % 140);
  g->drawFastHLine(cx - 68, scanY, 136, kDim);
  g->drawFastHLine(cx - 68, scanY + 1, 136, kGreen);
  g->fillCircle(cx, cy, 10, kGreen);

  uint8_t nextStep = min<uint8_t>(6, elapsed / 500);
  if (nextStep != splashStep_) {
    splashStep_ = nextStep;
    g->fillRect(340, 499, 380, 24, kBg);
    text(g, 365, 503, kBootSteps[min<uint8_t>(5, nextStep)], nextStep == 6 ? kBright : kGreen, 2);
  }
  uint16_t progress = min<uint16_t>(452, elapsed * 452UL / 3100UL);
  g->fillRect(286, 540, progress, 6, kGreen);
}

void PipBoyTerminal::drawHome_(Arduino_GFX *g) {
  text(g, 42, 94, "VAULT-TEC PERSONAL INFORMATION PROCESSOR", kBright, 2);
  const char *labels[] = {"STAT", "MAP", "ITEMS", "DATA", "RADIO"};
  const char *sub[] = {"condition and weather", "explore the wastes", "carry and inspect", "images and logs", "holotapes and signal"};
  for (uint8_t i = 0; i < 5; ++i) {
    int16_t col = i % 3, row = i / 3, x = 42 + col * 322, y = 130 + row * 190;
    box(g, x, y, 292, 154, i == 4 && media_.playing());
    text(g, x + 22, y + 25, String("0") + (i + 1), kDim, 2);
    text(g, x + 22, y + 70, labels[i], kGreen, 3); text(g, x + 22, y + 118, sub[i], kInk);
  }
  text(g, 42, 505, "TOUCH A MODULE TO BEGIN // ALL ASSETS ARE LOCAL", kInk, 2);
}

void PipBoyTerminal::drawStat_(Arduino_GFX *g) {
  text(g, 48, 98, "STATUS // PERSONAL VITALS", kBright, 2);
  box(g, 44, 128, 430, 370); box(g, 510, 128, 470, 370);
  text(g, 72, 160, "LOCAL TIME", kDim); text(g, 72, 200, clockText(), kGreen, 4);
  const char *labels[] = {"HP", "AP", "RAD", "POWER"}; uint8_t values[] = {82, 64, 11, 91};
  for (uint8_t i = 0; i < 4; ++i) { int16_t y = 290 + i * 45; text(g, 72, y, labels[i], kInk, 2); g->drawRect(190, y, 230, 20, kDim); g->fillRect(192, y + 2, values[i] * 226 / 100, 16, i == 2 ? kAlert : kGreen); }
  text(g, 540, 160, "OUTDOOR WEATHER", kDim); text(g, 540, 205, PIPBOY_WEATHER_LABEL, kGreen, 2);
  text(g, 540, 260, tempText(feeds_.weather.tempC), kBright, 4);
  text(g, 730, 260, feeds_.weatherValid ? feeds_.weather.condition : "AWAITING C6 LINK", kInk, 2);
  String wind = isnan(feeds_.weather.windKt) ? "--" : String(feeds_.weather.windKt, 0) + "KT";
  text(g, 540, 345, "WIND " + wind + " // HI " + tempText(feeds_.weather.hiC) + " // LO " + tempText(feeds_.weather.loC), kInk, 2);
  text(g, 540, 440, "TAP HERE TO REFRESH WEATHER", network_.connected() ? kGreen : kAlert, 2);
}

void PipBoyTerminal::drawStatClock_(Arduino_GFX *g) {
  if (g == nullptr) return;
  g->fillRect(70, 198, 330, 48, kBg);
  text(g, 72, 200, clockText(), kGreen, 4);
  lastDynamicDrawMs_ = millis();
}

void PipBoyTerminal::drawFallbackArt_(Arduino_GFX *g, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t variant) {
  g->drawRect(x, y, w, h, kGreen); int16_t cx = x + w / 2, cy = y + h / 2;
  if (variant % 3 == 0) { g->drawCircle(cx, cy, min(w, h) / 3, kGreen); g->drawLine(cx - 50, cy, cx + 50, cy, kGreen); g->drawLine(cx, cy - 50, cx, cy + 50, kGreen); }
  else if (variant % 3 == 1) { g->drawTriangle(cx, y + 30, x + 40, y + h - 30, x + w - 40, y + h - 30, kGreen); g->drawCircle(cx, cy + 28, 12, kGreen); }
  else { g->drawRoundRect(x + 55, y + 45, w - 110, h - 90, 18, kGreen); g->drawLine(cx, y + 60, cx, y + h - 60, kGreen); }
}

void PipBoyTerminal::drawMap_(Arduino_GFX *g) {
  text(g, 42, 96, "MAP // MOJAVE EXPLORATION GRID", kBright, 2);
  box(g, 42, 122, 694, 420); bool image = media_.drawMap(g, 50, 130, 676, 402);
  if (!image) {
    for (int16_t i = 0; i < 650; i += 50) { g->drawFastVLine(60 + i, 145, 360, kDim); if (i < 360) g->drawFastHLine(60, 145 + i, 650, kDim); }
    g->drawCircle(230, 270, 20, kBright); g->drawCircle(450, 370, 12, kGreen); g->drawCircle(580, 220, 15, kAlert);
    text(g, 104, 170, "VAULT 13", kInk); text(g, 465, 395, "OUTPOST", kInk); text(g, 590, 245, "DANGER", kAlert);
  }
  box(g, 760, 122, 220, 420); text(g, 785, 160, "MARKERS", kGreen, 2);
  text(g, 785, 220, "01 VAULT 13", kInk, 2); text(g, 785, 275, "02 OUTPOST", kInk, 2); text(g, 785, 330, "03 RADIO TOWER", kInk, 2);
  text(g, 785, 420, "MAP FILE", kDim); text(g, 785, 447, image ? "SD BMP LOADED" : "BUILT-IN GRID", image ? kGreen : kAlert);
}

void PipBoyTerminal::drawItems_(Arduino_GFX *g) {
  text(g, 42, 96, "ITEMS // LOADOUT INVENTORY", kBright, 2);
  for (uint8_t i = 0; i < 4; ++i) { int16_t y = 130 + i * 64; box(g, 42, y, 410, 54, selectedItem_ == i); text(g, 68, y + 18, String("0") + (i + 1), kDim, 2); text(g, 130, y + 18, kItems[i], selectedItem_ == i ? kBright : kInk, 2); }
  box(g, 488, 130, 492, 368); drawFallbackArt_(g, 555, 175, 350, 190, selectedItem_);
  text(g, 530, 392, kItems[selectedItem_], kGreen, 3); text(g, 530, 445, kItemNotes[selectedItem_], kInk, 2);
  text(g, 42, 530, "TAP AN ITEM FOR INSPECTION", kDim);
}

void PipBoyTerminal::drawData_(Arduino_GFX *g) {
  text(g, 42, 96, "DATA // ARCHIVE AND IMAGE VIEWER", kBright, 2);
  box(g, 42, 126, 610, 390); bool image = media_.drawImage(g, selectedImage_, 52, 136, 590, 370);
  if (!image) drawFallbackArt_(g, 150, 160, 390, 300, selectedImage_ + 1);
  box(g, 684, 126, 296, 390); text(g, 710, 160, "ARCHIVE", kGreen, 2);
  text(g, 710, 218, media_.imageCount() ? media_.imageName(selectedImage_) : "BUILT-IN TERMINAL ART", kInk, 2);
  text(g, 710, 300, "HOLOTAPE LOG", kDim); text(g, 710, 330, "\"The signal is weak,", kInk); text(g, 710, 352, "but it is still there.\"", kInk);
  text(g, 710, 445, "TAP IMAGE TO CYCLE", kBright, 2);
}

void PipBoyTerminal::drawRadio_(Arduino_GFX *g) {
  text(g, 42, 96, "RADIO // HOLOTAPE PLAYER", kBright, 2);
  box(g, 42, 128, 620, 340); text(g, 76, 165, media_.playing() ? "NOW PLAYING" : "RADIO SILENT", media_.playing() ? kGreen : kAlert, 2);
  text(g, 76, 220, media_.trackCount() ? media_.trackName(media_.activeTrack()) : "INSERT SD HOLOTAPE", kBright, 3);
  drawRadioWave_(g);
  box(g, 700, 128, 280, 340); text(g, 730, 170, "SIGNAL", kGreen, 2); text(g, 730, 230, "VOLUME " + String(media_.volume()) + "%", kInk, 2);
  g->drawRect(730, 270, 200, 18, kDim); g->fillRect(732, 272, media_.volume() * 196 / 100, 14, kGreen);
  text(g, 730, 340, media_.status(), kInk);
  box(g, 42, 500, 280, 48, media_.playing()); box(g, 370, 500, 280, 48); box(g, 700, 500, 280, 48);
  text(g, 115, 516, media_.playing() ? "STOP" : "PLAY", kBright, 2); text(g, 445, 516, "NEXT / TEST", kBright, 2); text(g, 770, 516, "VOLUME +", kBright, 2);
}

void PipBoyTerminal::drawRadioWave_(Arduino_GFX *g) {
  if (g == nullptr) return;
  g->fillRect(78, 330, 520, 110, kBg);
  for (int16_t i = 0; i < 500; i += 10) {
    int16_t peak = media_.playing() ? 15 + ((i * 17 + millis() / 18) % 85)
                                    : 10 + ((i * 7 + millis() / 70) % 24);
    g->drawFastVLine(85 + i, 390 - peak / 2, peak, media_.playing() ? kGreen : kDim);
  }
  lastDynamicDrawMs_ = millis();
}

void PipBoyTerminal::drawActiveTabPulse_(Arduino_GFX *g) {
  if (g == nullptr || millis() - tabPulseStartedMs_ > 420) return;
  int16_t x = static_cast<int16_t>(page_) * 171;
  g->fillRect(x + 2, 2, 166, 60, kPanel);
  g->drawRect(x, 0, 170, 64, kBright);
  int16_t sweep = x + 8 + ((millis() - tabPulseStartedMs_) * 148UL / 420UL);
  g->drawFastVLine(sweep, 4, 56, kGreen);
  text(g, x + 46, 25, kPageNames[page_], kBright, 2);
}

void PipBoyTerminal::drawHomeActivity_(Arduino_GFX *g) {
  if (g == nullptr) return;
  const bool lit = (millis() / 420) % 2 == 0;
  g->fillRect(770, 498, 190, 24, kBg);
  text(g, 770, 503, lit ? "SYS READY  [*]" : "SYS READY  [ ]", lit ? kGreen : kDim, 2);
}

void PipBoyTerminal::drawStatPulse_(Arduino_GFX *g) {
  if (g == nullptr) return;
  constexpr int16_t barX = 192;
  constexpr int16_t barY = 292;
  constexpr int16_t width = 82 * 226 / 100;
  int16_t marker = barX + ((millis() / 90) % max<int16_t>(1, width - 5));
  g->fillRect(barX, barY, width, 16, kGreen);
  g->fillRect(marker, barY, 5, 16, kBright);
}

void PipBoyTerminal::drawMapPulse_(Arduino_GFX *g) {
  if (g == nullptr) return;
  const int16_t cx = 945;
  const int16_t cy = 225;
  const int16_t radius = 5 + ((millis() / 180) % 11);
  g->fillRect(926, 206, 38, 38, kBg);
  g->drawCircle(cx, cy, radius, kDim);
  g->fillCircle(cx, cy, 4, kGreen);
}

void PipBoyTerminal::drawItemsSweep_(Arduino_GFX *g) {
  if (g == nullptr) return;
  const int16_t x = 42;
  const int16_t y = 130 + selectedItem_ * 64;
  const int16_t width = 410;
  const int16_t position = (millis() / 14) % (width - 42);
  g->drawRect(x, y, width, 54, kBright);
  g->drawRect(x + 2, y + 2, width - 4, 50, kGreen);
  g->drawFastHLine(x + 10 + position, y + 2, 32, kBright);
}

void PipBoyTerminal::drawDataActivity_(Arduino_GFX *g) {
  if (g == nullptr) return;
  const bool cursor = (millis() / 400) % 2 == 0;
  g->fillRect(710, 470, 220, 20, kBg);
  text(g, 710, 472, cursor ? "ARCHIVE LINK: ACTIVE_" : "ARCHIVE LINK: ACTIVE", cursor ? kGreen : kDim);
}

void PipBoyTerminal::tickAnimation_(Arduino_GFX *g) {
  if (g == nullptr) return;
  const uint32_t now = millis();
  if (now - tabPulseStartedMs_ <= 420 && now - lastAnimationDrawMs_ >= 70) {
    drawActiveTabPulse_(g);
    lastAnimationDrawMs_ = now;
    return;
  }
  uint16_t interval = 420;
  if (page_ == kPipItems) interval = 120;
  else if (page_ == kPipRadio) interval = media_.playing() ? 120 : 420;
  else if (page_ == kPipStat) interval = 180;
  if (now - lastAnimationDrawMs_ < interval) return;
  switch (page_) {
    case kPipHome: drawHomeActivity_(g); break;
    case kPipStat: drawStatPulse_(g); break;
    case kPipMap: drawMapPulse_(g); break;
    case kPipItems: drawItemsSweep_(g); break;
    case kPipData: drawDataActivity_(g); break;
    case kPipRadio: drawRadioWave_(g); break;
  }
  lastAnimationDrawMs_ = now;
}

void PipBoyTerminal::draw_() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas(); if (g == nullptr) return;
  drawHeader_(g);
  switch (page_) { case kPipHome: drawHome_(g); break; case kPipStat: drawStat_(g); break; case kPipMap: drawMap_(g); break; case kPipItems: drawItems_(g); break; case kPipData: drawData_(g); break; case kPipRadio: drawRadio_(g); break; }
  lastDynamicDrawMs_ = millis(); dirty_ = false;
  lastAnimationDrawMs_ = millis();
#endif
}

void PipBoyTerminal::printStatus(Print &out) const {
  out.print(F("[pipboy] page=")); out.print(kPageNames[page_]); out.print(F(" sd=")); out.print(media_.sdReady() ? F("mounted") : F("fallback"));
  out.print(F(" tracks=")); out.print(media_.trackCount()); out.print(F(" images=")); out.print(media_.imageCount());
  out.print(F(" audio=")); out.print(media_.status()); out.print(F(" wifi=")); out.println(network_.connected() ? F("connected") : F("offline"));
  out.println(F("[proof] compile-ready; SD, touch, speaker, C6 time/weather require device validation"));
}
void PipBoyTerminal::printTouch(Print &out) const { out.printf("[pipboy] touch count=%lu last=%d,%d\n", static_cast<unsigned long>(touchCount_), lastTouchX_, lastTouchY_); }
void PipBoyTerminal::commandRadio(const String &args, Print &out) {
  String value = args; value.trim();
  if (value == "stop") media_.stop(); else if (value == "next" || value == "play") nextTrack(); else if (value.startsWith("volume ")) media_.setVolume(value.substring(7).toInt()); else if (value == "test") media_.speakerTest();
  out.println(media_.status()); dirty_ = true;
}
void PipBoyTerminal::commandWeather(Print &out) { bool ok = world_.refresh(feeds_, "weather"); out.println(ok ? F("[pipboy] weather refreshed") : F("[pipboy] weather unavailable")); dirty_ = true; }
void PipBoyTerminal::commandStorage(Print &out) const { out.println(media_.status()); }
