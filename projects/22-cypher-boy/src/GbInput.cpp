#include "GbInput.h"

namespace {

// Local point-in-rect rather than Widgets::hitRect: the whole Widgets
// namespace only exists on USE_DISPLAY + P4 builds, and mapPoint() has to stay
// available headless so the selftest can verify every hitbox with no panel.
inline bool inBox(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h) {
  return px >= x && px < (int16_t)(x + w) && py >= y && py < (int16_t)(y + h);
}

// The gamepad is built at boot by GbInput::buildLayout() rather than hardcoded,
// so the whole pad follows wherever the screen sits. Order matters only for
// drawing; hit-testing scans the whole table.
GbHitbox kLayout[9];
uint8_t kLayoutCount = 0;
int16_t kDpadX = 0, kDpadY = 0, kDpadW = 0, kDpadH = 0;

}  // namespace

const GbHitbox *GbInput::layout(uint8_t &count) {
  count = kLayoutCount;
  return kLayout;
}

void GbInput::dpadBounds(int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
  x = kDpadX; y = kDpadY; w = kDpadW; h = kDpadH;
}

void GbInput::buildLayout(int16_t vx, int16_t vy, int16_t vw, int16_t vh) {
  const int16_t W = 1024, H = 600;
  const int16_t leftZone = vx;             // margin left of the screen
  const int16_t rightZone = W - (vx + vw);  // margin right of it

  // D-pad: a cross of four square arms, centred in the left margin and set a
  // little below the screen's midline so it falls under the thumb.
  const int16_t arm = min<int16_t>(80, leftZone / 3);
  const int16_t cx = leftZone / 2;
  const int16_t cy = vy + (int16_t)(vh * 0.52f);
  const int16_t half = arm / 2;

  uint8_t n = 0;
  kLayout[n++] = {(int16_t)(cx - half), (int16_t)(cy - arm - half), arm, arm, GB_BTN_UP, "UP", kCtlArm};
  kLayout[n++] = {(int16_t)(cx - half), (int16_t)(cy + half), arm, arm, GB_BTN_DOWN, "DOWN", kCtlArm};
  kLayout[n++] = {(int16_t)(cx - arm - half), (int16_t)(cy - half), arm, arm, GB_BTN_LEFT, "LEFT", kCtlArm};
  kLayout[n++] = {(int16_t)(cx + half), (int16_t)(cy - half), arm, arm, GB_BTN_RIGHT, "RIGHT", kCtlArm};

  kDpadX = cx - arm - half;
  kDpadY = cy - arm - half;
  kDpadW = arm * 3;
  kDpadH = arm * 3;

  // A / B: round, offset diagonally the way a real handheld has them.
  const int16_t r = min<int16_t>(55, rightZone / 4);
  const int16_t ax = W - r - 24, ay = cy - 40;
  const int16_t bx = vx + vw + r + 18, by = cy + 45;
  kLayout[n++] = {(int16_t)(ax - r), (int16_t)(ay - r), (int16_t)(r * 2), (int16_t)(r * 2), GB_BTN_A, "A", kCtlRound};
  kLayout[n++] = {(int16_t)(bx - r), (int16_t)(by - r), (int16_t)(r * 2), (int16_t)(r * 2), GB_BTN_B, "B", kCtlRound};

  // START / SELECT: under the screen when the bottom strip is deep enough,
  // otherwise tucked beneath each side cluster (which is what a taller console
  // like the NES needs).
  const int16_t pw = 150, ph = 46;
  const int16_t bottom = H - (vy + vh);
  if (bottom >= 62) {
    const int16_t sy = vy + vh + 10;
    kLayout[n++] = {(int16_t)(vx + 40), sy, pw, ph, GB_BTN_SELECT, "SELECT", kCtlPill};
    kLayout[n++] = {(int16_t)(vx + vw - 40 - pw), sy, pw, ph, GB_BTN_START, "START", kCtlPill};
  } else {
    kLayout[n++] = {(int16_t)(cx - pw / 2), (int16_t)(kDpadY + kDpadH + 14), pw, ph, GB_BTN_SELECT, "SELECT", kCtlPill};
    kLayout[n++] = {(int16_t)(vx + vw + (rightZone - pw) / 2), (int16_t)(by + r + 14), pw, ph, GB_BTN_START, "START", kCtlPill};
  }

  // MENU stays out of the way, top-left under the header.
  kLayout[n++] = {20, 90, 110, 46, 0, "MENU", kCtlPill};
  kLayoutCount = n;
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

  // Multi-touch button state: OR together the button under EVERY active
  // contact, so you can hold a direction and press A at the same time - the
  // single-point path could only ever report one button, which made movement +
  // action games unplayable. The GT911 reports up to 5 points; they share the
  // same cached sample CrowTouch just read, so this adds no extra I2C traffic.
  uint32_t bits = 0;
#if USE_DISPLAY
  CrowDisplay::TouchPointData pts[5];
  const uint8_t n = CrowDisplay::touchPoints(pts, 5);
  for (uint8_t i = 0; i < n; i++) {
    bits |= mapPoint(pts[i].x, pts[i].y);
  }
#else
  if (touch_.down()) bits = mapPoint(touch_.x(), touch_.y());
#endif
  buttons_ = bits;

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
