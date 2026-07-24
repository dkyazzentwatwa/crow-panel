#include "GbInput.h"

namespace {

// Local point-in-rect rather than Widgets::hitRect: the whole Widgets
// namespace only exists on USE_DISPLAY + P4 builds, and mapPoint() has to stay
// available headless so the selftest can verify every hitbox with no panel.
inline bool inBox(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return px >= x && px < (int16_t)(x + w) && py >= y && py < (int16_t)(y + h);
}

// On-screen gamepad, laid out to the right of the x3 viewport.
// The viewport occupies x = 40..520, so every control starts past x = 540 and
// nothing overlaps the game picture. Targets are >= 80px so they are usable
// with a thumb. These are first-pass values - expect to tune them once it is
// on glass (the `touch` command prints the mapped point to help).
const GbHitbox kLayout[] = {
    // D-pad cluster (left of the button cluster), classic cross arrangement.
    {620, 330,  90,  90, GB_BTN_UP,     "UP"},
    {620, 510,  90,  90, GB_BTN_DOWN,   "DOWN"},
    {530, 420,  90,  90, GB_BTN_LEFT,   "LEFT"},
    {710, 420,  90,  90, GB_BTN_RIGHT,  "RIGHT"},

    // A / B, offset diagonally the way a real Game Boy has them.
    // A sits at x=920 (not 930) so its right edge lands on 1020, inside the
    // 1024px panel - the selftest asserts every control fits on screen.
    {920, 380, 100, 100, GB_BTN_A,      "A"},
    {820, 450, 100, 100, GB_BTN_B,      "B"},

    // START / SELECT, centred under the pad.
    {620, 250, 130,  56, GB_BTN_SELECT, "SELECT"},
    {780, 250, 130,  56, GB_BTN_START,  "START"},

    // MENU: edge-triggered, returns to the ROM picker.
    {900,  86, 110,  56, 0,             "MENU"},
};
const uint8_t kLayoutCount = sizeof(kLayout) / sizeof(kLayout[0]);

}  // namespace

const GbHitbox *GbInput::layout(uint8_t &count) {
  count = kLayoutCount;
  return kLayout;
}

uint32_t GbInput::mapPoint(int16_t px, int16_t py) const {
  uint32_t bits = 0;
  for (uint8_t i = 0; i < kLayoutCount; i++) {
    const GbHitbox &b = kLayout[i];
    if (b.bit == 0) continue;  // MENU is handled separately (edge-triggered)
    if (inBox(px, py, b.x, b.y, b.w, b.h)) {
      bits |= b.bit;
    }
  }
  return bits;
}

void GbInput::tick() {
  touch_.tick();
  menuEdge_ = false;

  // Serial-injected presses win for a couple of frames so `button a` actually
  // registers with the emulator, which samples the pad once per frame.
  if (injectFrames_ > 0) {
    injectFrames_--;
    buttons_ = injected_;
    if (injectFrames_ == 0) injected_ = 0;
    return;
  }

  buttons_ = touch_.down() ? mapPoint(touch_.x(), touch_.y()) : 0;

  // MENU on release, so dragging off it cancels.
  if (touch_.releasedEdge()) {
    uint8_t n;
    const GbHitbox *L = layout(n);
    for (uint8_t i = 0; i < n; i++) {
      if (L[i].bit != 0) continue;
      if (inBox(touch_.releaseX(), touch_.releaseY(), L[i].x, L[i].y, L[i].w, L[i].h)) {
        menuEdge_ = true;
      }
    }
  }
}
