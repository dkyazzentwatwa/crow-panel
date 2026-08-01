#include "config/ProjectConfig.h"
#include "src/StickEngine.h"
#include "src/StickLayout.h"

#include <CrowPanelShared.h>
// StickEngine.h pulls this in transitively, but gHid below needs the type
// directly -- include explicitly rather than lean on the transitive path.
#include <CrowHidBackend.h>

static EventLog gEvents;
static SerialCommandRouter gRouter;
static HidBackend gHid;
static StickProfile gProfile;
static StickEngine gEngine;

static const char *hatName(uint8_t hat) {
  switch (hat) {
    case kHatUp: return "U";
    case kHatUpRight: return "UR";
    case kHatRight: return "R";
    case kHatDownRight: return "DR";
    case kHatDown: return "D";
    case kHatDownLeft: return "DL";
    case kHatLeft: return "L";
    case kHatUpLeft: return "UL";
    default: return "-";
  }
}

static void cmdStatus(const String &args) {
  (void)args;
  Serial.print("mode=");
  Serial.print(gHid.gamepadLive() ? "LIVE" : "MOCK");
  Serial.print(" profile=");
  Serial.print(gProfile.name);
  Serial.print(" keys=");
  Serial.print(gProfile.keyCount);
  Serial.print(" hat=");
  Serial.print(hatName(gEngine.hat()));
  Serial.print(" buttons=0x");
  Serial.print(gEngine.buttons(), HEX);
  Serial.print(" polls=");
  Serial.print(gEngine.polls());
  Serial.print(" changes=");
  Serial.print(gEngine.changes());
  // changes= (engine-observed hat/button transitions) and reports= (actual
  // USB reports HidBackend has sent) are NOT the same number by design: the
  // first gamepadState() call always sends (gamepadStateValid_ starts
  // false) and a USB re-enumeration forces a resync send, neither of which
  // is a hat/button change on our side. A growing gap here is a normal
  // resync signal, not a bug.
  Serial.print(" reports=");
  Serial.println(gHid.gamepadReports());
}

static void cmdBench(const String &args) {
  (void)args;
  Serial.print("worst poll (touch->send, OUR half only) = ");
  Serial.print(gEngine.worstPollUs());
  Serial.println(" us");
  Serial.println("NOTE: excludes the GT911's own sense+report time (~10 ms at 100 Hz).");
  Serial.println("NOTE: the FIRST reading after boot is inflated -- that poll forces an");
  Serial.println("unthrottled GT911 read plus the initial neutral report; run `bench` once");
  Serial.println("to clear it before trusting a steady-state number.");
  gEngine.resetBench();
}

// CLAUDE.md: every sketch answers help, status, and history. begin() registers
// help itself; status and history are ours.
static void cmdHistory(const String &args) {
  (void)args;
  gEvents.printHistory(Serial);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  gRouter.begin(Serial, "Cypher Stick");
  gRouter.on("status", "stick status", cmdStatus);
  gRouter.on("bench", "worst observed poll time", cmdBench);
  gRouter.on("history", "recent events", cmdHistory);

  stickDefaultProfile(gProfile);

  // manualFlush=true: Arduino_GFX otherwise cache-syncs on every primitive.
  // With it off we sync once per changed key via CrowDisplay::flush(x,y,w,h),
  // which is exactly the "single-key feedback" hot path the API documents.
  CrowDisplay::begin(activeHardwareProfile(), "Cypher Stick", true);

  gHid.begin(&Serial, &gEvents, "Cypher Stick", "cypherstick");
  gEngine.begin(&gHid, &gProfile);
  gEvents.add("stick ready");

  Serial.println("Cypher Stick ready");
}

void loop() {
  gEngine.poll();
  // gHid.service() is deliberately not called here: it only drains pending
  // key/consumer releases and mouse-move coalescing for the keyboard/mouse
  // HID surfaces, neither of which gamepadState() uses (it bypasses
  // tapKey()/kHoldMs entirely -- a fightstick is all holds). Nothing on this
  // path ever creates a pending release for service() to flush. This stops
  // being true the day keyboard-output mode lands alongside the gamepad
  // path, at which point service() becomes required again.
  gRouter.poll();
}
