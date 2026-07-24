#ifndef NFC_FIELD_LAB_NFC_LAB_UI_H
#define NFC_FIELD_LAB_NFC_LAB_UI_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include <CrowPanelShared.h>  // CrowTouch, HardwareProfile, Widgets chrome
#include "NfcTypes.h"
#include "BadgeRegistry.h"

// Touch-first inspection console for the NFC Field Lab / BadgeOps Pro project,
// drawn on CrowDisplay::canvas() with the shared Widgets toolkit (dark "ops"
// palette, FreeSans fonts, headerBar/tabBar chrome). Replaces the generic
// OpsDashboard tiles with five bespoke screens:
//
//   SCAN  -> UID hero card: tag type, technology, and the active reader.
//   NDEF  -> decoded public NDEF record preview (record type + payload).
//   APDU  -> read-only Type 4 exchange as a stepper; advance one step at a
//            time, command and response side by side.
//   BADGE -> the BadgeOps grant/deny decision on top of the scanned tag.
//   FILES -> the tag file/application list plus exported artifacts.
//
// The read-only boundary is kept visible in the chrome on every screen: this is
// lab inspection (SELECT + READ BINARY only), never payment or credential work,
// and nothing is ever written back to a tag.
//
// tick() reads touch and returns a typed event for the .ino to execute; the UI
// never mutates application state itself. showScreen() lets the serial commands
// mirror tab navigation so every touch action has a serial equivalent.

enum NfcScreen : uint8_t {
  SCR_SCAN = 0,
  SCR_NDEF,
  SCR_APDU,
  SCR_BADGE,
  SCR_FILES,
  SCR_COUNT,
};

enum NfcLabEvent : uint8_t {
  EVT_NONE = 0,
  EVT_SCAN_NEXT,    // read the next tag (re-derives NDEF/APDU/badge)
  EVT_APDU_STEP,    // advance the APDU stepper one step (wraps)
  EVT_APDU_RESET,   // reset the APDU stepper to step 0
};

// Everything the screens render. Owned by the sketch, populated from the
// active NfcReader(s) and the BadgeRegistry; the UI only reads it.
struct NfcLabState {
  bool hasTag = false;
  NfcUidRead tag;

  bool hasNdef = false;
  NdefPreview ndef;

  bool hasApdu = false;
  SafeApduRead apdu;

  bool badgeEvaluated = false;
  bool badgeMatched = false;
  BadgeDecision badge;
  BadgeRecord badgeRecord;

  bool ndefSupported = true;   // an active reader can preview Type 4 NDEF
  bool apduSupported = true;   // an active reader can run the Type 4 read
  uint8_t apduStep = 0;        // visible step index [0, NfcLabUi::kApduStepCount)

  String readerLabel = "none";
  String banner;
};

class NfcLabUi {
 public:
  // Number of steps in the read-only Type 4 NDEF exchange stepper.
  static const uint8_t kApduStepCount = 6;

  bool begin(const char *readerLabel);

  // Reads touch and redraws when dirty. Returns an event to execute, or
  // EVT_NONE. Safe to call on headless builds (returns EVT_NONE, never draws).
  NfcLabEvent tick(NfcLabState &st);

  void showScreen(NfcScreen s);   // serial-command parity for tab navigation
  NfcScreen screen() const { return screen_; }
  void markDirty();

  // `touch` diagnostic: raw + mapped point, tap count, and current screen.
  void printTouch(Print &out) const;
  const char *screenName() const;

 private:
#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
  NfcLabEvent handleTouch_(NfcLabState &st);
  void draw_(NfcLabState &st);
  void drawChrome_(NfcLabState &st);
  void drawFooter_();
  void drawScan_(NfcLabState &st);
  void drawNdef_(NfcLabState &st);
  void drawApdu_(NfcLabState &st);
  void drawBadge_(NfcLabState &st);
  void drawFiles_(NfcLabState &st);

  bool ready_ = false;
  bool dirty_ = true;
#endif
  CrowTouch touch_;             // shared debounced touch (never-pressed headless)
  NfcScreen screen_ = SCR_SCAN;
};

#endif  // NFC_FIELD_LAB_NFC_LAB_UI_H
