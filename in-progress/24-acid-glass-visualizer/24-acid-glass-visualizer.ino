// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8), including the full
// display + PPA + touch + SD audio + hosted-C6 remote combination. The isolated
// CPU visual path, basic touch UI, and factory preset loading are FIELD-PROVEN.
// PPA status, SD, audible playback, and AP association still require the staged
// device checklist in TECHNICAL.md.

#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/AcidGlassApp.h"

AcidGlassApp acidGlass;
SerialCommandRouter router;
EventLog eventLog;

String nextWord(String &line) {
  line.trim();
  int space = line.indexOf(' ');
  if (space < 0) {
    String word = line;
    line = "";
    return word;
  }
  String word = line.substring(0, space);
  line = line.substring(space + 1);
  line.trim();
  return word;
}

bool sendControl(const char *action, const char *key, int32_t value) {
  ControlEvent event;
  event.source = ControlSource::kSerial;
  strlcpy(event.action, action, sizeof(event.action));
  strlcpy(event.key, key != nullptr ? key : "", sizeof(event.key));
  event.value = value;
  bool accepted = acidGlass.control(event);
  if (!accepted) Serial.println(F("[acid-glass] control rejected"));
  return accepted;
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "acid-glass", eventLog.size(), &router);
  acidGlass.printStatus(Serial);
}

void cmdScene(const String &args) {
  String value = args;
  value.trim();
  if (value.length() == 0 || value == "list") {
    acidGlass.printScenes(Serial);
    return;
  }
  if (value == "next") { sendControl("next", "", 0); return; }
  if (value == "prev" || value == "previous") { sendControl("previous", "", 0); return; }
  int8_t index = AcidGlassVisuals::sceneIndex(value);
  if (index < 0) { Serial.println(F("[scene] unknown; use scene list")); return; }
  sendControl("scene", "", index);
}

void cmdPalette(const String &args) {
  String value = args;
  value.trim();
  if (value.length() == 0 || value == "list") {
    acidGlass.printPalettes(Serial);
    return;
  }
  if (value == "next") {
    uint8_t next = (acidGlass.state().palette + 1) % kAcidPaletteCount;
    sendControl("palette", "", next);
    return;
  }
  int8_t index = AcidGlassVisuals::paletteIndex(value);
  if (index < 0) { Serial.println(F("[palette] unknown; use palette list")); return; }
  sendControl("palette", "", index);
}

void cmdSet(const String &args) {
  String rest = args;
  String key = nextWord(rest);
  if (key.length() == 0 || rest.length() == 0) {
    Serial.println(F("[set] speed|zoom|intensity|warp|feedback|trails|symmetry|pixels|hue|sensitivity|macrox|macroy|brightness|safe VALUE"));
    return;
  }
  sendControl("set", key.c_str(), rest.toInt());
}

void cmdRandomize(const String &) { sendControl("randomize", "", 0); }

void cmdPreset(const String &args) {
  String rest = args;
  String action = nextWord(rest);
  action.toLowerCase();
  if (action.length() == 0 || action == "list") {
    acidGlass.printPresets(Serial);
    return;
  }
  int slot = rest.toInt();
  if ((action != "load" && action != "save") || slot < 1 || slot > kAcidPresetCount) {
    Serial.println(F("[preset] list | load 1-16 | save 1-16"));
    return;
  }
  sendControl("preset", action.c_str(), slot - 1);
}

void cmdTrack(const String &args) {
  String rest = args;
  String action = nextWord(rest);
  action.toLowerCase();
  if (action.length() == 0 || action == "list") { acidGlass.printTracks(Serial); return; }
  if (action == "play") { sendControl("play", "", rest.length() ? rest.toInt() : -1); return; }
  if (action == "next") { sendControl("tracknext", "", 0); return; }
  if (action == "prev" || action == "previous") { sendControl("trackprev", "", 0); return; }
  if (action == "stop") { sendControl("stop", "", 0); return; }
  Serial.println(F("[track] list | play [index] | next | prev | stop"));
}

void cmdVolume(const String &args) { sendControl("volume", "", args.toInt()); }

void cmdDemo(const String &args) {
  String value = args;
  value.toLowerCase();
  value.trim();
  sendControl("demo", "", value != "off" && value != "0");
}

void cmdQuality(const String &args) {
  String value = args;
  value.toLowerCase();
  value.trim();
  if (value == "auto") { sendControl("quality", "", 0); return; }
  if (value == "performance" || value == "perf") { sendControl("quality", "", 1); return; }
  if (value == "quality") { sendControl("quality", "", 2); return; }
  Serial.println(F("[quality] auto | performance | quality"));
}

void cmdFreeze(const String &) { sendControl("freeze", "", -1); }
void cmdHud(const String &) { sendControl("hud", "", -1); }
void cmdTouch(const String &) { acidGlass.printTouch(Serial); }
void cmdRemote(const String &) { acidGlass.printStatus(Serial); }

void cmdBench(const String &args) {
  uint16_t seconds = constrain(args.toInt(), 1, 600);
  acidGlass.benchmark(Serial, seconds);
}

void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void setup() {
  Logger::begin(115200);
  Logger::info("app", "Acid Glass ESP32-P4 visual instrument");
  printHardwareProfile(Serial, activeHardwareProfile());
  acidGlass.begin();
  eventLog.add("Acid Glass booted");

  router.begin(Serial, "acid-glass");
  router.on("status", "visual, audio, SD, remote and proof status", cmdStatus, "system");
  router.on("history", "recent event history", cmdHistory, "system");
  router.on("touch", "last mapped GT911 contact", cmdTouch, "system");
  router.on("bench", "SECONDS (1-600), report live frame telemetry", cmdBench, "system");
  router.on("remote", "show C6 AP and client status", cmdRemote, "system");
  router.on("scene", "list|next|prev|NAME|INDEX", cmdScene, "visual");
  router.on("palette", "list|next|NAME|INDEX", cmdPalette, "visual");
  router.on("set", "PARAM VALUE (0-255 unless noted)", cmdSet, "visual");
  router.on("randomize", "generate a new deterministic look", cmdRandomize, "visual");
  router.on("preset", "list|load 1-16|save 1-16", cmdPreset, "visual");
  router.on("demo", "on|off synthetic beat showcase", cmdDemo, "performance");
  router.on("quality", "auto|performance|quality frame pacing", cmdQuality, "performance");
  router.on("freeze", "toggle the current frozen frame", cmdFreeze, "performance");
  router.on("hud", "toggle performance telemetry overlay", cmdHud, "performance");
  router.on("track", "list|play [index]|next|prev|stop", cmdTrack, "audio");
  router.on("volume", "0-100", cmdVolume, "audio");
}

void loop() {
  router.poll();
  acidGlass.tick();
  delay(1);
}
