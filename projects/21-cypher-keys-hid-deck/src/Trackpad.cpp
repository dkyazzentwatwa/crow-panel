#include "Trackpad.h"

#include "HidBackend.h"
#include "TouchInput.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#endif

namespace {
// Trackpad screen regions (below the status bar at y 0..36).
constexpr int16_t kPadX = 22, kPadY = 92, kPadW = 736, kPadH = 378;
constexpr int16_t kScrollX = 780, kScrollY = 92, kScrollW = 222, kScrollH = 378;
constexpr int16_t kBtnY = 486, kBtnH = 78;
constexpr int16_t kLBtnX = 22, kLBtnW = 478;
constexpr int16_t kRBtnX = 524, kRBtnW = 478;

// A tap (vs a drag): small travel, released quickly.
constexpr int16_t kTapSlop = 12;
constexpr uint32_t kTapMs = 300;
// Pixels of vertical travel in the scroll strip per one wheel detent.
constexpr int16_t kScrollStep = 18;

bool in(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

int16_t scaleMove(int16_t delta) {
  int32_t v = (int32_t)delta * CYPHER_KEYS_TRACKPAD_GAIN_NUM /
              CYPHER_KEYS_TRACKPAD_GAIN_DEN;
  if (v > CYPHER_KEYS_TRACKPAD_MAX_STEP) v = CYPHER_KEYS_TRACKPAD_MAX_STEP;
  if (v < -CYPHER_KEYS_TRACKPAD_MAX_STEP) v = -CYPHER_KEYS_TRACKPAD_MAX_STEP;
  return (int16_t)v;
}
}  // namespace

Trackpad::Zone Trackpad::zoneAt(int16_t x, int16_t y) {
  if (in(x, y, kPadX, kPadY, kPadW, kPadH)) return kZonePad;
  if (in(x, y, kScrollX, kScrollY, kScrollW, kScrollH)) return kZoneScroll;
  if (in(x, y, kLBtnX, kBtnY, kLBtnW, kBtnH)) return kZoneLeft;
  if (in(x, y, kRBtnX, kBtnY, kRBtnW, kBtnH)) return kZoneRight;
  return kZoneNone;
}

void Trackpad::reset() {
  active_ = kZoneNone;
  scrollAccum_ = 0;
  moved_ = false;
}

void Trackpad::update(TouchInput &touch, HidBackend &hid) {
  if (touch.pressedEdge()) {
    active_ = zoneAt(touch.x(), touch.y());
    lastX_ = touch.x();
    lastY_ = touch.y();
    startX_ = touch.x();
    startY_ = touch.y();
    startMs_ = millis();
    scrollAccum_ = 0;
    moved_ = false;
    if (active_ == kZoneLeft) hid.mouseButton(1, true);
    if (active_ == kZoneRight) hid.mouseButton(2, true);
    return;
  }

  if (touch.down() && active_ != kZoneNone) {
    int16_t dx = touch.x() - lastX_;
    int16_t dy = touch.y() - lastY_;
    if (active_ == kZonePad) {
      if (dx != 0 || dy != 0) hid.mouseMove(scaleMove(dx), scaleMove(dy));
      if (abs(touch.x() - startX_) > kTapSlop ||
          abs(touch.y() - startY_) > kTapSlop)
        moved_ = true;
    } else if (active_ == kZoneScroll) {
      scrollAccum_ += dy;
      while (scrollAccum_ >= kScrollStep) {
        hid.mouseScroll(-1);  // finger down = content up = wheel down
        scrollAccum_ -= kScrollStep;
      }
      while (scrollAccum_ <= -kScrollStep) {
        hid.mouseScroll(1);
        scrollAccum_ += kScrollStep;
      }
    }
    lastX_ = touch.x();
    lastY_ = touch.y();
    return;
  }

  if (touch.releasedEdge()) {
    switch (active_) {
      case kZonePad:
        if (!moved_ && (millis() - startMs_) < kTapMs) hid.mouseClick(1);
        break;
      case kZoneLeft:
        hid.mouseButton(1, false);
        break;
      case kZoneRight:
        hid.mouseButton(2, false);
        break;
      default:
        break;
    }
    active_ = kZoneNone;
  }
}

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
void Trackpad::draw(Arduino_GFX *g, const DeckTheme &theme) const {
  if (g == nullptr) return;
  g->fillRect(0, 40, 1024, 560, theme.bg);

  // Pad surface.
  Widgets::panel(g, kPadX, kPadY, kPadW, kPadH, 16, theme.surface, 2, theme.line);
  Widgets::text(g, kPadX + kPadW / 2, kPadY + kPadH / 2 - 8, "TRACKPAD",
                Widgets::fontL(), theme.muted, Widgets::kCenter);
  Widgets::text(g, kPadX + kPadW / 2, kPadY + kPadH / 2 + 16,
                "drag to move  -  tap to click", Widgets::fontS(), theme.muted,
                Widgets::kCenter);

  // Scroll strip.
  Widgets::panel(g, kScrollX, kScrollY, kScrollW, kScrollH, 16, theme.surface, 2,
                 theme.line);
  Widgets::text(g, kScrollX + kScrollW / 2, kScrollY + kScrollH / 2 - 8, "SCROLL",
                Widgets::fontS(), theme.muted, Widgets::kCenter);

  // Click buttons.
  Widgets::panel(g, kLBtnX, kBtnY, kLBtnW, kBtnH, 14, theme.surfaceHi, 2,
                 theme.accent);
  Widgets::text(g, kLBtnX + kLBtnW / 2, kBtnY + 28, "LEFT CLICK", Widgets::fontL(),
                theme.ink, Widgets::kCenter);
  Widgets::panel(g, kRBtnX, kBtnY, kRBtnW, kBtnH, 14, theme.surfaceHi, 2,
                 theme.accent);
  Widgets::text(g, kRBtnX + kRBtnW / 2, kBtnY + 28, "RIGHT CLICK",
                Widgets::fontL(), theme.ink, Widgets::kCenter);
}
#endif
