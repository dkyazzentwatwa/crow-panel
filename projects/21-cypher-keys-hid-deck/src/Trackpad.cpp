#include "Trackpad.h"

#include "HidBackend.h"
#include "KeysLayout.h"  // pad/scroll/button rects and the tap thresholds
#include "KeysTouch.h"

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
#include <CrowPanelShared.h>
#include <Arduino_GFX_Library.h>
#endif

namespace {
int16_t scaleMove(int16_t delta) {
  int32_t v = (int32_t)delta * CYPHER_KEYS_TRACKPAD_GAIN_NUM /
              CYPHER_KEYS_TRACKPAD_GAIN_DEN;
  if (v > CYPHER_KEYS_TRACKPAD_MAX_STEP) v = CYPHER_KEYS_TRACKPAD_MAX_STEP;
  if (v < -CYPHER_KEYS_TRACKPAD_MAX_STEP) v = -CYPHER_KEYS_TRACKPAD_MAX_STEP;
  return (int16_t)v;
}
}  // namespace

Trackpad::Zone Trackpad::zoneAt(int16_t x, int16_t y) {
  if (KeysLayout::hitPadSurface(x, y)) return kZonePad;
  if (KeysLayout::hitScrollStrip(x, y)) return kZoneScroll;
  if (KeysLayout::hitLeftButton(x, y)) return kZoneLeft;
  if (KeysLayout::hitRightButton(x, y)) return kZoneRight;
  return kZoneNone;
}

void Trackpad::reset() {
  active_ = kZoneNone;
  scrollAccum_ = 0;
  moved_ = false;
}

void Trackpad::update(KeysTouch &touch, HidBackend &hid) {
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
      if (abs(touch.x() - startX_) > KeysLayout::kTapSlop ||
          abs(touch.y() - startY_) > KeysLayout::kTapSlop)
        moved_ = true;
    } else if (active_ == kZoneScroll) {
      scrollAccum_ += dy;
      while (scrollAccum_ >= KeysLayout::kScrollStep) {
        hid.mouseScroll(-1);  // finger down = content up = wheel down
        scrollAccum_ -= KeysLayout::kScrollStep;
      }
      while (scrollAccum_ <= -KeysLayout::kScrollStep) {
        hid.mouseScroll(1);
        scrollAccum_ += KeysLayout::kScrollStep;
      }
    }
    lastX_ = touch.x();
    lastY_ = touch.y();
    return;
  }

  if (touch.releasedEdge()) {
    switch (active_) {
      case kZonePad:
        if (!moved_ && (millis() - startMs_) < KeysLayout::kTapMs)
          hid.mouseClick(1);
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
  using namespace KeysLayout;  // every coordinate below comes from KeysLayout.h
  g->fillRect(0, kTrackpadBgY, kScreenW, kTrackpadBgH, theme.bg);

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
  Widgets::panel(g, kLBtnX, kClickBtnY, kLBtnW, kClickBtnH, 14, theme.surfaceHi,
                 2, theme.accent);
  Widgets::text(g, kLBtnX + kLBtnW / 2, kClickBtnY + 28, "LEFT CLICK",
                Widgets::fontL(), theme.ink, Widgets::kCenter);
  Widgets::panel(g, kRBtnX, kClickBtnY, kRBtnW, kClickBtnH, 14, theme.surfaceHi,
                 2, theme.accent);
  Widgets::text(g, kRBtnX + kRBtnW / 2, kClickBtnY + 28, "RIGHT CLICK",
                Widgets::fontL(), theme.ink, Widgets::kCenter);
}
#endif
