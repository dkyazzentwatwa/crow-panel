#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/AudioOutput.h"
#include "src/PadSampleEngine.h"
#include "src/StepTransport.h"
#include "src/VisualVoices.h"

OpsDashboard dashboard;
SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
AudioOutput &audio = audioOutput();
PadSampleEngine pads;
StepTransport transport;
VisualVoices voices;

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
  return word;
}

void refreshMpc(const String &banner) {
  dashboard.setTile(0, "Pads", "4x4", "touch trigger grid");
  dashboard.setTile(1, "Steps", transport.patternString(), "16-step sequencer");
  dashboard.setTile(2, "BPM", String(transport.bpm()), "tempo");
  dashboard.setTile(3, "Play", transport.playing() ? "ON" : "OFF", "transport");
  dashboard.setTile(4, "Audio", audio.hardwareReady() ? "I2S" : "mock", audio.modeName());
  dashboard.setTile(5, "Voice", pads.lastLabel(), voices.summary());
  dashboard.setBanner(banner);
#if USE_AUDIO
  dashboard.setFooter("USE_AUDIO=1 compile path: generated I2S click, samples/proof still staged");
#else
  dashboard.setFooter("MPC default is silent/mock; enable USE_AUDIO only for compile-tested I2S experiments");
#endif
}

void cmdStatus(const String &) { printSystemStatus(Serial, "cypher-tune-mpc", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdPad(const String &args) {
  uint8_t pad = constrain(args.toInt(), 1, PadSampleEngine::kPadCount);
  String message;
  pads.trigger(pad, message);
  if (transport.recording()) {
    transport.recordPad(pad);
  }
  Serial.println(String("[pad] ") + message);
  eventLog.add(message);
  dashboard.setDetail("Pad Trigger", message + "|" + voices.detail());
  refreshMpc("pad triggered");
}

void cmdStep(const String &args) {
  String rest = args;
  uint8_t step = constrain(nextWord(rest).toInt(), 1, StepTransport::kStepCount);
  uint8_t pad = constrain(rest.toInt(), 1, PadSampleEngine::kPadCount);
  bool active = transport.toggleStep(step, pad);
  Serial.println(String("[step] ") + String(step) + "=" + (active ? "on" : "off") + " pad=" + String(pad));
  dashboard.setDetail("Step Sequencer", transport.detailString());
  refreshMpc("step toggled");
}

void cmdBpm(const String &args) {
  int next = args.toInt();
  if (!transport.setBpm(next)) {
    Serial.println(F("[bpm] rejected; range is 40-240"));
  }
  Serial.println(String("[bpm] ") + String(transport.bpm()));
  dashboard.setDetail("Tempo", String("BPM ") + String(transport.bpm()) + "|Range 40-240|16th-step clock");
  refreshMpc("tempo updated");
}

void cmdPlay(const String &) {
  transport.play();
  eventLog.add("Transport play");
  dashboard.setDetail("Transport", String("Playing|BPM ") + String(transport.bpm()) + "|" + transport.detailString());
  refreshMpc("transport playing");
}

void cmdStop(const String &) {
  transport.stop();
  eventLog.add("Transport stop");
  dashboard.setDetail("Transport", "Stopped|Pattern retained|Serial remains smoke path");
  refreshMpc("transport stopped");
}

void cmdRecord(const String &) {
  transport.toggleRecord();
  Serial.println(transport.recording() ? F("[record] on") : F("[record] off"));
  dashboard.setDetail("Record", transport.recording() ? "Overdub armed|Pad hits mark current step" : "Overdub off|Playback remains available");
  refreshMpc("record toggled");
}

void cmdPattern(const String &) {
  String row = transport.patternString();
  Serial.println(String("[pattern] ") + row);
  dashboard.setDetail("Pattern", transport.detailString());
  refreshMpc("pattern printed");
}

void cmdSamples(const String &) {
  Serial.println(String("[samples] ") + pads.sampleMap());
  dashboard.setDetail("Sample Map", pads.sampleMap() + "|factory refs are placeholders; no files required");
  refreshMpc("sample map shown");
}

void cmdVoices(const String &) {
  Serial.println(String("[voices] ") + voices.summary());
  dashboard.setDetail("Visual Voices", voices.detail());
  refreshMpc("voices shown");
}

void cmdAudio(const String &) {
  Serial.println(String("[audio] ") + audio.statusLine());
  dashboard.setDetail("Audio Path", audio.statusLine() + "|Hardware sound is compile-ready only");
  refreshMpc("audio status");
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Tune MPC");
  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);
  storage.begin("cypher-mpc");
  audio.begin(profile, Serial);
  voices.begin();
  pads.begin(audio, voices);
  transport.begin(92);
  dashboard.begin("CYPHER TUNE", "MPC TOUCH SURFACE", "SILENT");
  refreshMpc("groovebox ready");
  eventLog.add("Cypher Tune MPC booted");
  router.begin(Serial, "mpc");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("pad", "pad <1-16>", cmdPad);
  router.on("step", "step <1-16> [pad]", cmdStep);
  router.on("bpm", "bpm <40-240>", cmdBpm);
  router.on("play", "start 16-step transport", cmdPlay);
  router.on("stop", "stop transport", cmdStop);
  router.on("record", "toggle record", cmdRecord);
  router.on("pattern", "print 16-step pattern", cmdPattern);
  router.on("samples", "print pad sample map", cmdSamples);
  router.on("voices", "print visual voice state", cmdVoices);
  router.on("audio", "print audio path status", cmdAudio);
}

void loop() {
  router.poll();
  String stepEvent;
  if (transport.tick(pads, stepEvent)) {
    Serial.println(String("[transport] ") + stepEvent);
    eventLog.add(stepEvent);
    dashboard.setDetail("Transport Step", stepEvent + "|" + voices.detail());
    refreshMpc("step fired");
  }
  voices.tick();
  audio.tick();
  dashboard.tick();
  delay(20);
}
