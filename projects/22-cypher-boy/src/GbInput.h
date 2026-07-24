#ifndef CYPHER_BOY_INPUT_H
#define CYPHER_BOY_INPUT_H

#include "../config/ProjectConfig.h"
#include "GameBoyHost.h"  // GbButton
#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowTouch

// One on-screen control. `bit` is the GbButton it feeds, or 0 for the special
// MENU control (which is edge-triggered rather than held).
struct GbHitbox {
  int16_t x, y, w, h;
  uint32_t bit;
  const char *label;
};

// Turns touches into a Game Boy pad bitfield.
//
// Gameplay controls are sampled as HELD regions, not release edges - you have
// to be able to hold a direction while pressing A. Multiple controls can be
// down at once (UP+A), so the bits are OR-ed. MENU is the one exception: it
// fires on release so a stray drag across it cannot exit your game.
//
// mapPoint() is pure, which lets the selftest verify every hitbox with no
// panel attached.
class GbInput {
 public:
  void tick();  // call once per frame

  uint32_t buttons() const { return buttons_; }
  bool menuPressed() const { return menuEdge_; }

  uint32_t mapPoint(int16_t x, int16_t y) const;
  static const GbHitbox *layout(uint8_t &count);
  static bool isMenu(const GbHitbox &box) { return box.bit == 0; }

  // Release edge, used by the picker screen (tap-to-select) and MENU.
  bool releasedEdge() const { return touch_.releasedEdge(); }
  int16_t releaseX() const { return touch_.releaseX(); }
  int16_t releaseY() const { return touch_.releaseY(); }

  // Diagnostics for the `touch` serial command.
  bool down() const { return touch_.down(); }
  int16_t x() const { return touch_.x(); }
  int16_t y() const { return touch_.y(); }
  int16_t rawX() const { return touch_.rawX(); }
  int16_t rawY() const { return touch_.rawY(); }
  uint32_t tapCount() const { return touch_.count(); }

  // Serial parity: `button` injects a pad state for one frame.
  void injectButtons(uint32_t bits) { injected_ = bits; injectFrames_ = 2; }

 private:
  CrowTouch touch_;
  uint32_t buttons_ = 0;
  uint32_t injected_ = 0;
  uint8_t injectFrames_ = 0;
  bool menuEdge_ = false;
};

#endif
