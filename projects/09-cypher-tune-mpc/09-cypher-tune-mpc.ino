#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
bool steps[16];
bool playing = false;
bool recording = false;
uint16_t bpm = 92;
uint8_t lastPad = 0;

void refreshMpc(const String &banner) {
  dashboard.setTile(0, "Pads", "4x4", "touch trigger grid");
  dashboard.setTile(1, "Steps", "16", "pattern sequencer");
  dashboard.setTile(2, "BPM", String(bpm), "tempo");
  dashboard.setTile(3, "Play", playing ? "ON" : "OFF", "silent transport");
  dashboard.setTile(4, "Record", recording ? "ON" : "OFF", "overdub mock");
  dashboard.setTile(5, "Voice", lastPad ? String(lastPad) : "none", "visual only");
  dashboard.setBanner(banner);
  dashboard.setFooter("MPC v1 is silent/mock audio; USE_AUDIO remains future hardware work");
}

void cmdStatus(const String &) { printSystemStatus(Serial, "cypher-tune-mpc", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdPad(const String &args) {
  lastPad = constrain(args.toInt(), 1, 16);
  Serial.println(String("[pad] trigger ") + String(lastPad));
  eventLog.add(String("Pad ") + String(lastPad));
  dashboard.setDetail("Pad Trigger", String("Pad ") + String(lastPad) + " fired|Visual voice only|No audio output in v1");
  refreshMpc("pad triggered");
}

void cmdStep(const String &args) {
  uint8_t step = constrain(args.toInt(), 1, 16);
  steps[step - 1] = !steps[step - 1];
  Serial.println(String("[step] ") + String(step) + "=" + (steps[step - 1] ? "on" : "off"));
  dashboard.setDetail("Step Sequencer", String("Step ") + String(step) + (steps[step - 1] ? " ON" : " OFF") + "|16-step grid staged");
  refreshMpc("step toggled");
}

void cmdBpm(const String &args) {
  int next = args.toInt();
  if (next >= 40 && next <= 240) bpm = next;
  Serial.println(String("[bpm] ") + String(bpm));
  dashboard.setDetail("Tempo", String("BPM ") + String(bpm) + "|Range 40-240|Mock transport");
  refreshMpc("tempo updated");
}

void cmdPlay(const String &) {
  playing = true;
  eventLog.add("Transport play");
  dashboard.setDetail("Transport", String("Playing|BPM ") + String(bpm) + "|Silent transport in v1");
  refreshMpc("transport playing");
}

void cmdStop(const String &) {
  playing = false;
  eventLog.add("Transport stop");
  dashboard.setDetail("Transport", "Stopped|Pattern retained|Silent transport in v1");
  refreshMpc("transport stopped");
}

void cmdRecord(const String &) {
  recording = !recording;
  Serial.println(recording ? F("[record] on") : F("[record] off"));
  dashboard.setDetail("Record", recording ? "Overdub armed|Pad hits mark steps" : "Overdub off|Playback remains available");
  refreshMpc("record toggled");
}

void cmdPattern(const String &) {
  String row = "";
  for (uint8_t i = 0; i < 16; i++) row += steps[i] ? "x" : ".";
  Serial.println(String("[pattern] ") + row);
  dashboard.setDetail("Pattern", row + "|x = active step|. = empty step");
  refreshMpc("pattern printed");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Tune MPC");
  printHardwareProfile(Serial, activeHardwareProfile());
  storage.begin("cypher-mpc");
  dashboard.begin("CYPHER TUNE", "MPC TOUCH SURFACE", "SILENT");
  refreshMpc("groovebox ready");
  eventLog.add("Cypher Tune MPC booted");
  router.begin(Serial, "mpc");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("pad", "pad <1-16>", cmdPad);
  router.on("step", "step <1-16>", cmdStep);
  router.on("bpm", "bpm <40-240>", cmdBpm);
  router.on("play", "start silent transport", cmdPlay);
  router.on("stop", "stop transport", cmdStop);
  router.on("record", "toggle record", cmdRecord);
  router.on("pattern", "print 16-step pattern", cmdPattern);
}

void loop() {
  router.poll();
  dashboard.tick();
  delay(20);
}
