#include "StickLayout.h"

#include <string.h>

int stickHitTest(const StickProfile &p, int16_t x, int16_t y) {
  // Iterate backwards so the last (topmost) key wins an overlap.
  for (int i = (int)p.keyCount - 1; i >= 0; i--) {
    const StickKey &k = p.keys[i];
    if (x < k.x || x >= k.x + k.w) continue;
    if (y < k.y || y >= k.y + k.h) continue;
    if (k.shape == kShapeRound) {
      const int32_t cx = k.x + k.w / 2;
      const int32_t cy = k.y + k.h / 2;
      const int32_t rx = k.w / 2;
      const int32_t ry = k.h / 2;
      if (rx <= 0 || ry <= 0) continue;
      const int32_t dx = x - cx;
      const int32_t dy = y - cy;
      // Normalised ellipse test in fixed point, no floating point on the
      // stick task. The numerator is widened to 64-bit before squaring:
      // dx is bounded by the key's half-width (rx), and dx*dx*10000 alone
      // can already exceed INT32_MAX once dx reaches 464 — i.e. a key
      // WIDTH of ~928px, well within what an editor could someday produce
      // on a 1024-wide panel.
      const int64_t ex = (int64_t)dx * dx * 10000;
      const int64_t ey = (int64_t)dy * dy * 10000;
      if (ex / (rx * rx) + ey / (ry * ry) > 10000) {
        continue;
      }
    }
    return i;
  }
  return -1;
}

StickState stickResolve(const StickProfile &p, const int *hits, int hitCount) {
  StickState s = {0, 0, false, false, false, false};
  for (int i = 0; i < hitCount; i++) {
    const int idx = hits[i];
    if (idx < 0 || idx >= (int)p.keyCount) continue;  // palm / stray contact

    // Set for every valid hit regardless of bind (including kBindNone) —
    // this is the only signal that survives SOCD collapsing two opposing
    // directions to neutral while both keys are still physically held. See
    // StickState::keysHeld.
    s.keysHeld |= (1u << idx);

    const uint8_t bind = p.keys[idx].bind;
    if (stickBindIsDirection(bind)) {
      switch (bind) {
        case kBindUp: s.up = true; break;
        case kBindDown: s.down = true; break;
        case kBindLeft: s.left = true; break;
        case kBindRight: s.right = true; break;
      }
    } else if (stickBindIsButton(bind)) {
      s.buttons |= (1u << stickBindButton(bind));
    }
    // kBindNone and the reserved 4-15 range contribute nothing further.
  }
  return s;
}

void stickDefaultProfile(StickProfile &p) {
  memset(&p, 0, sizeof p);
  strncpy(p.name, "Default", sizeof p.name - 1);
  p.socdPolicy = kSocdUpPriority;

  // Left hand: four directions, leverless arrangement. Panel is 1024x600.
  const int16_t s = 96;   // key size
  const int16_t g = 12;   // gap
  const int16_t lx = 60;  // left cluster origin
  const int16_t ly = 300;

  struct Seed { const char *label; int16_t x, y; uint8_t bind; uint8_t key; };
  const Seed seeds[] = {
    {"<",  lx,                 ly,             kBindLeft,  'a'},
    {"v",  lx + s + g,         ly,             kBindDown,  's'},
    {">",  lx + 2 * (s + g),   ly,             kBindRight, 'd'},
    {"^",  lx + s + g,         ly + s + g,     kBindUp,    ' '},
    // Right hand: six attack buttons, two rows of three.
    {"LP", 540,                ly - s - g,     kBindButton0 + 0, 'u'},
    {"MP", 540 + s + g,        ly - s - g,     kBindButton0 + 1, 'i'},
    {"HP", 540 + 2 * (s + g),  ly - s - g,     kBindButton0 + 2, 'o'},
    {"LK", 540,                ly,             kBindButton0 + 3, 'j'},
    {"MK", 540 + s + g,        ly,             kBindButton0 + 4, 'k'},
    {"HK", 540 + 2 * (s + g),  ly,             kBindButton0 + 5, 'l'},
  };
  // The loop bound below is p.keyCount, computed AT RUNTIME from this same
  // array — -Werror cannot catch a seed table that outgrows StickProfile's
  // fixed-size `keys` array (e.g. a future "add START/SELECT" edit past 20
  // seeds), which would silently overwrite keyCount and socdPolicy with no
  // diagnostic. This catches it at compile time instead.
  static_assert(sizeof seeds / sizeof seeds[0] <= STICK_LAYOUT_MAX_KEYS,
                "seed table outgrew StickProfile::keys");

  p.keyCount = (uint8_t)(sizeof seeds / sizeof seeds[0]);
  for (uint8_t i = 0; i < p.keyCount; i++) {
    StickKey &k = p.keys[i];
    strncpy(k.label, seeds[i].label, sizeof k.label - 1);
    k.label[sizeof k.label - 1] = '\0';
    k.x = seeds[i].x;
    k.y = seeds[i].y;
    k.w = s;
    k.h = s;
    k.color = 0;
    k.shape = kShapeRound;
    k.bind = seeds[i].bind;
    k.key = seeds[i].key;
    k.reserved = 0;
  }
}
