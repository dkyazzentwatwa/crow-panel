#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/HidDeck.h"

HidDeck deck;
SerialCommandRouter router;
EventLog eventLog;

void cmdStatus(const String &args) {
  (void)args;
  printSystemStatus(Serial, "cypher-keys", eventLog.size());
  deck.printStatus(Serial);
}

void cmdHistory(const String &args) {
  (void)args;
  eventLog.printHistory(Serial);
}

void cmdHid(const String &args) { (void)args; deck.printHid(Serial); }
void cmdKey(const String &args) { deck.commandKey(args); }
void cmdCombo(const String &args) { deck.commandCombo(args); }
void cmdTap(const String &args) { deck.commandTap(args); }
void cmdPreset(const String &args) { deck.commandPreset(args); }
void cmdMode(const String &args) { deck.commandMode(args); }
void cmdMouse(const String &args) { deck.commandMouse(args); }
void cmdClick(const String &args) { deck.commandClick(args); }
void cmdScroll(const String &args) { deck.commandScroll(args); }
void cmdMedia(const String &args) { deck.commandMedia(args); }
void cmdDictate(const String &args) { (void)args; deck.commandDictate(); }
void cmdTheme(const String &args) { deck.commandTheme(args); }
void cmdOutput(const String &args) { deck.commandOutput(args); }
void cmdBle(const String &args) { deck.commandBle(args); }
void cmdTouch(const String &args) { (void)args; deck.printTouchDiagnostics(Serial); }

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Keys HID Deck");
  printHardwareProfile(Serial, activeHardwareProfile());
  deck.begin(&eventLog);
  eventLog.add("Cypher Keys booted");

  router.begin(Serial, "cypher-keys");
  router.on("status", "mode, HID backend, preset, and proof state", cmdStatus);
  router.on("history", "recent deck events", cmdHistory);
  router.on("hid", "HID backend mode and interface list", cmdHid);
  router.on("key", "type a literal string to the host", cmdKey);
  router.on("combo", "send a shortcut: combo cmd+c | cmd+shift+4 | ctrl+up", cmdCombo);
  router.on("tap", "fire macro slot N in the active preset: tap 0", cmdTap);
  router.on("preset", "switch macro preset: preset next | <name>", cmdPreset);
  router.on("mode", "switch view: mode deck|trackpad", cmdMode);
  router.on("mouse", "move the cursor: mouse <dx> <dy>", cmdMouse);
  router.on("click", "mouse click: click l|r", cmdClick);
  router.on("scroll", "mouse wheel: scroll <steps>", cmdScroll);
  router.on("media", "media key: media volup|voldn|mute|play|brightup|brightdn", cmdMedia);
  router.on("dictate", "tap F5 (macOS dictation/mic key)", cmdDictate);
  router.on("theme", "switch UI theme: theme next | <name>", cmdTheme);
  router.on("out", "output: out usb|ble|toggle", cmdOutput);
  router.on("ble", "bluetooth: ble status|clear", cmdBle);
  router.on("touch", "print raw and mapped touch diagnostics", cmdTouch);
}

void loop() {
  router.poll();
  deck.tick();
  // Tight loop for low input latency and a smooth trackpad. Touch is throttled
  // internally to ~8 ms (GT911's native rate), and HID releases are serviced
  // non-blocking, so this mainly cuts latency rather than busy-spinning.
  delay(2);
}
