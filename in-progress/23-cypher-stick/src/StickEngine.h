#ifndef CYPHER_STICK_STICK_ENGINE_H
#define CYPHER_STICK_STICK_ENGINE_H

#include "../config/ProjectConfig.h"
#include "SocdCleaner.h"
#include "StickLayout.h"
#include "StickTouch.h"

#include <CrowPanelShared.h>
// CrowPanelShared.h's umbrella deliberately does not pull in the HID stack
// (see shared/CrowPanelShared/CrowHid.h) -- projects that need HidBackend
// include this directly, same as project 21's HidDeck.h.
#include <CrowHidBackend.h>

// The latency-critical path: contacts -> hit-test -> SOCD -> one HID report.
//
// poll() allocates nothing, formats nothing, and draws nothing. Once Task 8
// pins it to a dedicated core those are hard requirements, not style choices.
class StickEngine {
 public:
  void begin(HidBackend *hid, StickProfile *profile);

  // One iteration of the input path. Safe to call as fast as you like; the
  // touch layer rate-limits the actual I2C read.
  void poll();

  // Diagnostics, read from the render side. Plain scalars so reading them from
  // another core is safe without a lock -- but that only covers the reads.
  // resetBench() below is a cross-core WRITE that races poll()'s own
  // read-modify-write of worstPollUs_: an interleaved poll can land its
  // stale "worse" value back after the reset clears it. Benign for a
  // diagnostic counter -- do not treat it as an exact reset (same caveat as
  // HidBackend::reports_, see CrowHidBackend.h).
  uint32_t polls() const { return polls_; }
  // Engine-observed state TRANSITIONS, not USB reports sent. These diverge:
  // HidBackend sends unconditionally on the very first gamepadState() call
  // (gamepadStateValid_ starts false) and again after any USB re-enumeration
  // (resyncGamepad() clears that flag), neither of which changes hat_/
  // buttons_ here. Read gHid.gamepadReports() for the actual wire count; the
  // delta between the two is itself a useful resync diagnostic.
  uint32_t changes() const { return changes_; }
  uint8_t hat() const { return hat_; }
  uint32_t buttons() const { return buttons_; }
  // Per-key physical touch, independent of SOCD. The renderer highlights from
  // THIS, not from hat/buttons: under up-priority, Left+Right resolves the hat
  // to centre, and highlighting from the hat would leave both keys dark while
  // two fingers are visibly on the glass.
  uint32_t keysHeld() const { return keysHeld_; }
  uint32_t worstPollUs() const { return worstPollUs_; }
  void resetBench() { worstPollUs_ = 0; }

  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }

 private:
  HidBackend *hid_ = nullptr;
  StickProfile *profile_ = nullptr;
  StickTouch touch_;
  SocdMemory socd_;
  bool enabled_ = true;
  uint32_t polls_ = 0;
  uint32_t changes_ = 0;
  uint8_t hat_ = 0;
  uint32_t buttons_ = 0;
  uint32_t keysHeld_ = 0;
  uint32_t worstPollUs_ = 0;
};

#endif
