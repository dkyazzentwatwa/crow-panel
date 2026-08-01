// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8) with
// GFX Library for Arduino 1.6.x (Arduino_GFX) and SensorLib 0.4.x.
// NOT HARDWARE-VERIFIED. Panel timings and pins come from Elecrow's
// official V1.0 Arduino example (board_config.h): EK79007, 2-lane DSI.
//
// The EK79007 needs a vendor init sequence (lane-count, power/timing
// registers, sleep-out) before it shows anything - Arduino_GFX ships no
// EK79007 table, so we supply one below. If the screen still stays black
// while Serial keeps ticking, fall back to Elecrow's ESP32_Display_Panel
// path from example/V1.0/Arduino_Code/Lesson07-Turn_on_the_screen/ - only
// this file should need replacing. See docs/hardware-risk-register.md.

#include "DisplayBringup.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvGT911.hpp>
#include "esp_cache.h"
#include "Logger.h"
#include "Throttle.h"
#include "UiTheme.h"

namespace {

// Backlight PWM state. The pin and LEDC channel are owned here because
// beginPanel() is what attaches them; exposing raw ledcWrite() to callers would
// duplicate that knowledge in every project that wants to dim the panel.
uint8_t backlightPin = 0;
uint8_t backlightLevel = 255;
bool backlightReady = false;

// EK79007 vendor init, ported 1:1 from Espressif's esp_lcd_ek79007 driver
// (vendor_specific_init_default), which is the controller used on both the
// ESP32-P4-Function-EV-Board and the CrowPanel. Format is Arduino_GFX's
// lcd_init_cmd_t: { cmd, {data...}, data_bytes, delay_ms }. Sent over the
// DSI DBI/DCS channel by Arduino_ESP32DSIPanel::begin() before the DPI
// stream starts.
//   - 0xB2: PAD_CONTROL, 0x10 selects 2 DSI data lanes (matches the panel
//     constructed with 2 lanes @ 1000 Mbps).
//   - 0x80-0x86: vendor power/timing registers.
//   - 0x11: sleep-out, 120 ms settle. (The EK79007 needs no explicit
//     display-on; the DPI stream drives it once it is awake.)
static const lcd_init_cmd_t kEk79007InitOperations[] = {
    {0xB2, (uint8_t[]){0x10}, 1, 0},
    {0x80, (uint8_t[]){0x8B}, 1, 0},
    {0x81, (uint8_t[]){0x78}, 1, 0},
    {0x82, (uint8_t[]){0x84}, 1, 0},
    {0x83, (uint8_t[]){0x88}, 1, 0},
    {0x84, (uint8_t[]){0xA8}, 1, 0},
    {0x85, (uint8_t[]){0xE3}, 1, 0},
    {0x86, (uint8_t[]){0x88}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
};

constexpr uint16_t kWidth = 1024;
constexpr uint16_t kHeight = 600;
constexpr uint8_t kLineCount = 6;
// Adafruit-GFX classic font is 6x8 px at size 1.
constexpr uint8_t kTitleSize = 4;
constexpr uint8_t kLineSize = 3;
constexpr int16_t kMarginX = 24;
constexpr int16_t kTitleY = 16;
constexpr int16_t kFirstLineY = 88;
constexpr int16_t kLinePitch = 44;

Arduino_ESP32DSIPanel *dsiPanel = nullptr;
Arduino_DSI_Display *gfx = nullptr;
TouchDrvGT911 touch;
bool touchReady = false;
bool displayReady = false;
// When true, the panel was built with auto_flush=false and the app owns flushing
// via CrowDisplay::flush(). Default false preserves every existing project.
bool manualFlush = false;
bool touchSampled = false;
CrowDisplay::TouchPointData cachedPoints[TouchPoints::MAX_POINTS];
uint8_t cachedPointCount = 0;
uint32_t lastTouchSampleMs = 0;
uint16_t bgColor = 0;
uint16_t fgColor = 0;
uint16_t accentColor = 0;
Throttle touchLogThrottle(250);

void sampleTouch() {
  if (!displayReady || !touchReady) {
    cachedPointCount = 0;
    return;
  }

  // The GT911 point-info register is cleared by getTouchPoints(). Cache one
  // sample so CrowDisplay::tick() cannot consume the event before the app's
  // touchPoint()/touchPoints() call in the same loop. Polling also matches
  // Elecrow's official touch example and avoids depending on the IRQ trigger
  // polarity.
  uint32_t now = millis();
  if (touchSampled && now - lastTouchSampleMs < CROW_TOUCH_SAMPLE_MS) {
    return;
  }
  touchSampled = true;
  lastTouchSampleMs = now;
  cachedPointCount = 0;

  const TouchPoints &points = touch.getTouchPoints();
  uint8_t count = points.getPointCount();
  if (count > TouchPoints::MAX_POINTS) {
    count = TouchPoints::MAX_POINTS;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const TouchPoint &point = points.getPoint(i);
    cachedPoints[i].x = point.x;
    cachedPoints[i].y = point.y;
    cachedPoints[i].id = point.id;
  }
  cachedPointCount = count;
}

// UiTheme colors are 24-bit 0xRRGGBB; the panel wants RGB565.
uint16_t toColor565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

bool beginPanel(const HardwareProfile &profile) {
  const DisplayTiming &t = profile.displayTiming;
  dsiPanel = new Arduino_ESP32DSIPanel(
      t.hsyncPulse, t.hsyncBackPorch, t.hsyncFrontPorch,
      t.vsyncPulse, t.vsyncBackPorch, t.vsyncFrontPorch,
      t.preferSpeedHz, t.laneBitRateMbps);
  gfx = new Arduino_DSI_Display(kWidth, kHeight, dsiPanel, 0 /*rotation*/,
                                !manualFlush /*auto_flush*/, profile.display.lcdReset,
                                kEk79007InitOperations,
                                sizeof(kEk79007InitOperations) / sizeof(kEk79007InitOperations[0]));
  if (!gfx->begin()) {
    Logger::error("display", "DSI panel init failed");
    return false;
  }

  // Backlight: LEDC PWM, 30 kHz, active high (Elecrow board_config.h).
  ledcAttach(profile.display.backlight, 30000, 8);
  backlightPin = profile.display.backlight;
  backlightLevel = 255;
  ledcWrite(backlightPin, backlightLevel);
  backlightReady = true;
  return true;
}

void setBacklightLevel(uint8_t level) {
  if (!backlightReady) return;
  backlightLevel = level;
  ledcWrite(backlightPin, level);
}

void beginTouch(const HardwareProfile &profile) {
  touch.setPins(profile.touch.resetPin, profile.touch.interruptPin);
  // GT911_SLAVE_ADDRESS_UNKNOWN probes both 0x5D and 0x14 - the GT911
  // picks its address from INT strapping at power-on.
  touchReady = touch.begin(Wire, GT911_SLAVE_ADDRESS_UNKNOWN,
                           profile.touch.sda, profile.touch.scl);
  if (!touchReady) {
    Logger::error("touch", "GT911 not found on I2C (tried 0x5D and 0x14)");
    return;
  }
  // The GT911 reports at most the number of contacts its config register
  // allows, and panels commonly ship configured for a single point - which
  // looks exactly like a driver that "doesn't do multi-touch". Ask for the
  // full 5 so touchPoints() can actually return more than one finger. This
  // writes the RAM config + refresh flag (0x8100), not the flash-backed
  // config, so it is safe to do on every boot.
  touch.setMaxTouchPoint(TouchPoints::MAX_POINTS);
  Logger::info("touch", String("GT911 ready, model=") + touch.getModelName() +
                            ", max points=" + String(TouchPoints::MAX_POINTS));
}

void buildStatusScreen(const char *title) {
  gfx->fillScreen(bgColor);
  gfx->setTextWrap(false);
  gfx->setTextColor(accentColor);
  gfx->setTextSize(kTitleSize);
  gfx->setCursor(kMarginX, kTitleY);
  gfx->print(title);
}

}  // namespace

namespace CrowDisplay {

void setBacklight(uint8_t level) { setBacklightLevel(level); }
uint8_t backlight() { return backlightLevel; }

bool begin(const HardwareProfile &profile, const char *title, bool manual) {
  manualFlush = manual;
  const UiTheme &theme = defaultUiTheme();
  bgColor = toColor565(theme.background);
  fgColor = toColor565(theme.foreground);
  accentColor = toColor565(theme.accent);

  if (!beginPanel(profile)) {
    return false;
  }
  beginTouch(profile);
  buildStatusScreen(title);
  displayReady = true;
  flush();  // no-op unless manualFlush; makes the status screen appear
  Logger::info("display", "DSI status screen up (Adafruit-GFX-style API)");
  return true;
}

void flush() {
  if (gfx && manualFlush) {
    gfx->flush(true);
  }
}

void flush(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!gfx || !manualFlush) {
    return;
  }
  // Clamp to the panel, then sync the full rows spanning [y, y+h). Whole rows
  // are contiguous in the framebuffer, so this is one cache_msync of h rows -
  // still a fraction of the screen for a key/band, far cheaper than the full FB.
  if (y < 0) { h += y; y = 0; }
  if (h <= 0 || y >= kHeight) {
    return;
  }
  if (y + h > kHeight) { h = kHeight - y; }
  (void)x;
  (void)w;
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb) {
    return;
  }
  uint16_t *start = fb + (size_t)y * kWidth;
  size_t bytes = (size_t)h * kWidth * sizeof(uint16_t);
  esp_cache_msync(start, bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void setLine(uint8_t index, const String &text) {
  if (!displayReady || index >= kLineCount) {
    return;
  }
  int16_t y = kFirstLineY + index * kLinePitch;
  gfx->fillRect(0, y, kWidth, kLinePitch - 8, bgColor);
  gfx->setTextColor(fgColor);
  gfx->setTextSize(kLineSize);
  gfx->setCursor(kMarginX, y);
  gfx->print(text);
  flush(0, y, kWidth, kLinePitch - 8);  // no-op unless manualFlush
}

void tick() {
  sampleTouch();
  if (cachedPointCount > 0 && touchLogThrottle.ready()) {
    Logger::info("touch", "x=" + String(cachedPoints[0].x) +
                              " y=" + String(cachedPoints[0].y) +
                              " n=" + String(cachedPointCount));
  }
}

Arduino_GFX *canvas() {
  return gfx;
}

bool touchPoint(int16_t &x, int16_t &y) {
  sampleTouch();
  if (cachedPointCount == 0) {
    return false;
  }
  x = cachedPoints[0].x;
  y = cachedPoints[0].y;
  return true;
}

uint8_t touchPoints(TouchPointData *out, uint8_t maxPoints) {
  sampleTouch();
  uint8_t count = cachedPointCount < maxPoints ? cachedPointCount : maxPoints;
  for (uint8_t i = 0; i < count; ++i) {
    out[i] = cachedPoints[i];
  }
  return count;
}

}  // namespace CrowDisplay

#endif  // USE_DISPLAY && CONFIG_IDF_TARGET_ESP32P4
