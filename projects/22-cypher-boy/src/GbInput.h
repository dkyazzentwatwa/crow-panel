#ifndef CYPHER_BOY_INPUT_H
#define CYPHER_BOY_INPUT_H

#include "../config/ProjectConfig.h"
#include "GameBoyHost.h"  // GbButton
#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowTouch

// How a control is drawn. The hit rect is always a rectangle; the shape only
// affects rendering, so hit-testing stays trivial and headless-verifiable.
enum GbCtlShape : uint8_t {
  kCtlArm = 0,   // one arm of the D-pad cross
  kCtlRound,     // A / B
  kCtlPill,      // START / SELECT / MENU
};

// One on-screen control. `bit` is the GbButton it feeds, or 0 for the special
// MENU control (which is edge-triggered rather than held).
struct GbHitbox {
  int16_t x, y, w, h;
  uint32_t bit;
  const char *label;
  uint8_t shape;
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
  // Build the gamepad around a centred screen: D-pad in the left margin, A/B in
  // the right, START/SELECT under the screen when the bottom strip allows and
  // beneath the side clusters when it does not. Derived rather than hardcoded
  // so a different console's viewport produces a correct pad for free.
  static void buildLayout(int16_t vx, int16_t vy, int16_t vw, int16_t vh);
  static const GbHitbox *layout(uint8_t &count);
  // The D-pad cross body, for drawing it as one continuous piece.
  static void dpadBounds(int16_t &x, int16_t &y, int16_t &w, int16_t &h);
  static bool isMenu(const GbHitbox &box) { return box.bit == 0; }

  // Press/release edges. Release drives selection; press is what the idle
  // timer watches, so putting a finger down wakes the panel immediately.
  bool pressedEdge() const { return touch_.pressedEdge(); }
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
