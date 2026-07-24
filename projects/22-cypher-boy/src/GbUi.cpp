#include "GbUi.h"

#include "GbInput.h"
#include "GbVideo.h"
#include <CrowPanelShared.h>

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <Arduino_GFX_Library.h>
#endif

int8_t GbUi::pickerHit(int16_t px, int16_t py, uint8_t romCount) {
  if (romCount == 0) return -1;
  if (px < kRowX || px >= (int16_t)(kRowX + kRowW)) return -1;
  if (py < kRowTop) return -1;
  const int16_t row = (py - kRowTop) / kRowH;
  if (row < 0 || row >= (int16_t)romCount) return -1;
  return (int8_t)row;
}

bool GbUi::begin() {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  // manualFlush: draws only touch the cached framebuffer until flush(), which
  // turns Arduino_GFX's per-pixel cache sync into one sync per frame.
  ready_ = CrowDisplay::begin(activeHardwareProfile(), "Cypher Boy", true);
  if (!ready_) Logger::error("gbui", "display bring-up failed");
  return ready_;
#else
  ready_ = false;
  return false;
#endif
}

void GbUi::drawPicker(const GbRomStore &roms, int8_t selected) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;

  g->fillScreen(kBg);
  headerBar(g, "Cypher Boy", "Game Boy / GBC player",
            roms.ready() ? "SD READY" : "NO SD", roms.ready() ? kGreen : kAmber);

  if (roms.count() == 0) {
    text(g, kRowX, kRowTop + 8, "No ROMs found.", fontL(), kTextHi, kLeft);
    text(g, kRowX, kRowTop + 44, (String("Put .gb / .gbc files in ") + GB_ROM_DIR).c_str(),
         fontS(), kTextMut, kLeft);
  } else {
    for (uint8_t i = 0; i < roms.count(); i++) {
      const int16_t y = kRowTop + i * kRowH;
      const bool on = (int8_t)i == selected;
      panel(g, kRowX, y, kRowW, kRowH - 8, 10, on ? kSurfaceHi : kSurface, 1,
            on ? kAccent : kLine);
      text(g, kRowX + 18, y + 14, roms.name(i).c_str(), fontM(), on ? kTextHi : kTextMut, kLeft);
    }
  }

  // Right-hand explainer so the picker screen is not just an empty half.
  panel(g, 700, kRowTop, 284, 200, 12, kSurface, 1, kLine);
  text(g, 720, kRowTop + 18, "How to play", fontL(), kTextHi, kLeft);
  text(g, 720, kRowTop + 56, "1. Tap a ROM to launch", fontS(), kTextMut, kLeft);
  text(g, 720, kRowTop + 84, "2. Touch gamepad appears", fontS(), kTextMut, kLeft);
  text(g, 720, kRowTop + 112, "3. MENU returns here", fontS(), kTextMut, kLeft);
  text(g, 720, kRowTop + 140, "Saves are written to", fontS(), kTextMut, kLeft);
  text(g, 720, kRowTop + 166, GB_SAVE_DIR, fontS(), kAccent, kLeft);

  if (!roms.ready()) {
    text(g, kRowX, 560, roms.status().c_str(), fontS(), kAmber, kLeft);
  }

  CrowDisplay::flush();
  lastHeld_ = 0;
#else
  (void)roms;
  (void)selected;
#endif
}

void GbUi::drawPlayChrome(const String &title) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  using namespace Widgets;

  g->fillScreen(kBg);
  headerBar(g, title.c_str(), "running", "GB", kAccent);

  uint8_t n;
  const GbHitbox *L = GbInput::layout(n);
  for (uint8_t i = 0; i < n; i++) {
    const bool isMenu = (L[i].bit == 0);
    touchButton(g, L[i].x, L[i].y, L[i].w, L[i].h, L[i].label, false,
                isMenu ? kAmber : kAccent);
  }

  CrowDisplay::flush();
  lastHeld_ = 0;
#else
  (void)title;
#endif
}

void GbUi::drawButtonState(uint32_t heldBits) {
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  Arduino_GFX *g = CrowDisplay::canvas();
  if (!g || !ready_) return;
  if (heldBits == lastHeld_) return;  // only repaint controls that changed

  uint8_t n;
  const GbHitbox *L = GbInput::layout(n);
  for (uint8_t i = 0; i < n; i++) {
    if (L[i].bit == 0) continue;  // MENU never shows a held state
    const bool wasOn = (lastHeld_ & L[i].bit) != 0;
    const bool isOn = (heldBits & L[i].bit) != 0;
    if (wasOn == isOn) continue;
    Widgets::touchButton(g, L[i].x, L[i].y, L[i].w, L[i].h, L[i].label, isOn, Widgets::kAccent);
    CrowDisplay::flush(L[i].x, L[i].y, L[i].w, L[i].h);
  }
  lastHeld_ = heldBits;
#else
  (void)heldBits;
#endif
}
