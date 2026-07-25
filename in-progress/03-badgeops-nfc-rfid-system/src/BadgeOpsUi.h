#ifndef BADGEOPS_UI_H
#define BADGEOPS_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowTouch, CrowDisplay, Widgets, HardwareProfile
#include "BadgeReader.h"
#include "BadgeRegistry.h"
#include "AccessPolicy.h"

// Touch-first badge terminal for the 1024x600 CrowPanel DSI panel, built on the
// shared CrowDisplay bring-up + Widgets toolkit (dark "ops" palette, FreeSans
// fonts, cards/pills). Replaces the old six-line status screen with five
// screens keyed by an enum and a dirty_ flag:
//
//   TAP        - reader-state hero ("present a badge"), animated waiting pulse,
//                live reader identity, and quick-tap buttons for the demo.
//   RESULT     - granted/denied overlay with the badge card + policy reason,
//                auto-returning to TAP after a few seconds.
//   REGISTRY   - scrollable badge list (active / suspended / expired), tap a
//                row to inspect one badge (BADGE detail screen).
//   ATTENDANCE - audit log of every decision with timestamps.
//   READERS    - pick mock / PN532 / MFRC522 and show wiring state per reader.
//
// tick() reads touch and returns a typed event for the .ino to run through the
// tap pipeline; the UI never mutates application state (registry/policy/log)
// itself. All drawing lives behind USE_DISPLAY && ESP32P4 so the default
// headless build stays green and keeps identical Serial behavior.

enum BadgeScreen : uint8_t {
  SCR_TAP = 0,
  SCR_RESULT,      // transient decision overlay; auto-returns to TAP
  SCR_REGISTRY,    // scrollable badge list
  SCR_BADGE,       // one badge's detail (reached from REGISTRY)
  SCR_ATTENDANCE,  // decision audit log
  SCR_READERS,     // reader picker + wiring
  SCR_COUNT
};

// UI -> application events. The .ino executes these; the UI class stays a pure
// view over the state the .ino owns.
enum BadgeEventType : uint8_t {
  EV_NONE = 0,
  EV_TAP_UID,  // simulate a badge tap through the pipeline (uid in .arg)
};

struct BadgeOpsEvent {
  BadgeEventType type = EV_NONE;
  String arg;                 // uid for EV_TAP_UID
  bool userInitiated = true;  // touch/serial taps always present the RESULT screen
};

class BadgeOpsUi {
 public:
  // registry is borrowed (const, not owned); zone is the access zone shown in
  // the header and used only for display.
  void begin(const BadgeRegistry *registry, const char *zone);

  // Which reader is compiled-active (0 mock / 1 pn532 / 2 mfrc522), its driver
  // name, and whether its transport is ready. Refresh whenever it changes.
  void setReaderInfo(const char *driverName, uint8_t activeReaderIndex, bool activeReady);

  // Once per loop(): reads touch, animates, returns an event to execute.
  BadgeOpsEvent tick();

  // Called by the tap pipeline in the .ino. These update view state and let the
  // next tick() repaint; they do not draw directly.
  void renderTap(const BadgeRead &read);
  void renderDecision(const AccessDecision &decision, const BadgeRecord &record,
                      bool found, bool present);

  // Navigation / selection reachable from Serial (parity with touch).
  void showScreen(BadgeScreen screen);
  bool showScreenByName(const String &name);    // false on an unknown name
  bool selectReaderByName(const String &name);  // "mock" | "pn532" | "mfrc522"
  const char *screenName() const;
  BadgeScreen screen() const { return screen_; }
  uint8_t attendanceCount() const { return attCount_; }
  const char *lastDecisionUid() const;  // newest logged UID, "" when none

  // Diagnostics for the `touch` command.
  void printTouch(Print &out);

 private:
  static const uint8_t kReaderCount = 3;

  // Self-contained attendance ring. EventLog is generic + shared (String
  // messages); this keeps the structured fields the log screen renders with
  // fixed buffers, mirroring EventLog's storage policy.
  static const uint8_t kAttCap = 16;
  struct AttEntry {
    uint32_t ms = 0;
    bool granted = false;
    char uid[20] = {0};
    char name[24] = {0};
    char reason[28] = {0};
  };
  void pushAttendance(const AccessDecision &d, const BadgeRecord &r, bool found);
  const AttEntry &attAt(uint8_t visualIndex) const;  // 0 = most recent

  void markDirty();  // guarded so it compiles on headless builds

  // --- target-independent state (exists on every build) ---
  const BadgeRegistry *registry_ = nullptr;
  String zone_ = "lab";
  BadgeScreen screen_ = SCR_TAP;

  // last tap + decision (TAP + RESULT screens)
  String lastUid_;
  String lastReader_ = "mock";
  AccessDecision lastDecision_;
  BadgeRecord lastRecord_;
  bool lastFound_ = false;
  bool haveDecision_ = false;
  uint32_t resultShownMs_ = 0;

  // reader info
  char readerDriver_[24] = "mock";
  uint8_t activeReader_ = 0;    // compiled-active reader index
  bool activeReady_ = true;
  uint8_t selectedReader_ = 0;  // which reader card is expanded on READERS

  // browsing offsets
  uint8_t regScroll_ = 0;
  uint8_t regSelected_ = 0;  // badge shown on BADGE detail
  uint8_t attScroll_ = 0;

  // attendance ring
  AttEntry att_[kAttCap];
  uint8_t attHead_ = 0;   // next slot to overwrite
  uint8_t attCount_ = 0;  // saturates at kAttCap

  CrowTouch touch_;

#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  BadgeOpsEvent handleTouch();
  void draw();
  void drawTap();
  void drawResult();
  void drawRegistry();
  void drawBadge();
  void drawAttendance();
  void drawReaders();
  uint8_t visibleRegistryRows() const;
  uint8_t visibleAttendanceRows() const;

  bool ready_ = false;
  bool dirty_ = true;
  uint32_t lastDrawMs_ = 0;
#endif
};

#endif  // BADGEOPS_UI_H
