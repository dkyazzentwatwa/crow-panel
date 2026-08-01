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
  Serial.print(" sends=");
  Serial.println(gEngine.sends());
}

static void cmdBench(const String &args) {
  (void)args;
  Serial.print("worst poll (touch->send, OUR half only) = ");
  Serial.print(gEngine.worstPollUs());
  Serial.println(" us");
  Serial.println("NOTE: excludes the GT911's own sense+report time (~10 ms at 100 Hz).");
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
  gRouter.poll();
}
