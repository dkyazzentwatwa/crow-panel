#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>
#include "src/AudioEngine.h"
#include "src/SampleBank.h"
#include "src/Sequencer.h"
#include "src/BuiltinKit.h"
#include "src/LoopLibrary.h"
#include "src/SynthKit.h"
#include "src/TuneSplash.h"
#include "src/TuneUi.h"
#include "src/VisualVoices.h"
#include "src/WavLoader.h"

SerialCommandRouter router;
EventLog eventLog;
StorageManager storage;
SampleBank bank;         // bank 0: builtin kit (SD kits stage into bank 1)
SampleBank bankStaging;  // bank 1
Sequencer seq;
TuneUi ui;
VisualVoices voices;
uint8_t lastPadIdx = 255;

// Step-fire pads collected into one serial summary line per step.
String stepFireSummary;
uint8_t pendingStep = 0xFF;
String pendingPads;

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

SampleBank &activeBank() {
  SampleBank *b = audioEngine().activeBank();
  return b != nullptr ? *b : bank;
}

const char *lastPadLabel() {
  if (lastPadIdx >= SampleBank::kPadCount) {
    return "none";
  }
  return activeBank().pad(lastPadIdx).label;
}

// Fires a pad. With the engine running the trigger goes through the command
// ring and all feedback (visual voices, pad flash, record) comes back via
// engine events; in silent builds everything happens inline on the millis
// clock. Both serial `pad` and the touch surface come through here.
String firePad(uint8_t padIdx, uint8_t velocity) {
  const PadSound &sound = activeBank().pad(padIdx);
  uint8_t vel = velocity ? velocity : sound.defaultVelocity;
  String message = String("Pad ") + String(padIdx + 1) + " " + sound.label +
                   " vel=" + String(vel) + " sample=" + sound.sampleRef;
  if (audioEngine().running()) {
    EngineCommand cmd = {kCmdTrigger, padIdx, vel};
    audioEngine().post(cmd);
    message += " audio=engine";
  } else {
    lastPadIdx = padIdx;
    voices.trigger(padIdx + 1, sound.label, vel);
    ui.notePadFlash(padIdx, vel);
    message += " audio=mock";
    if (seq.recording()) {
      uint8_t step = seq.recordPadMillis(padIdx, vel, millis());
      message += String(" rec@step=") + String(step + 1);
    }
  }
  return message;
}

void transportPlay() {
  if (audioEngine().running()) {
    EngineCommand cmd = {kCmdPlay, 0, 0};
    audioEngine().post(cmd);
  } else {
    seq.play();
  }
}

void transportStop() {
  if (audioEngine().running()) {
    EngineCommand cmd = {kCmdStop, 0, 0};
    audioEngine().post(cmd);
  } else {
    seq.stop();
  }
}

void flushStepSummary() {
  if (pendingStep != 0xFF && pendingPads.length()) {
    String event = String("Step ") + String(pendingStep + 1) + " pads " + pendingPads;
    Serial.println(String("[transport] ") + event);
    eventLog.add(event);
  }
  pendingPads = "";
}

void onKitSwapped(uint8_t retiredBankIdx) {
  // The engine has killed every voice and flipped banks; only now is it safe
  // to free the retired bank's PSRAM buffers (labels/params stay for reuse).
  SampleBank &retired = retiredBankIdx == 1 ? bankStaging : bank;
  retired.freeAll();
  Serial.println(String("[kit] now ") + activeBank().kitName() +
                 " (bank " + String(audioEngine().activeBankIndex()) + ")");
}

// Loads a kit ("builtin" or an SD folder) into the inactive bank and asks
// the engine to flip. Without the engine (silent build) the load happens in
// place on bank 0 so labels/serial proof still work.
bool doKitLoad(const String &name) {
  bool engineUp = audioEngine().running();
  uint8_t stagingIdx = engineUp ? (audioEngine().activeBankIndex() ^ 1) : 0;
  SampleBank &staging = stagingIdx == 1 ? bankStaging : bank;
  String status;
  if (name == "builtin") {
    staging.beginDefaults(staging.engineRate());
    if (engineUp) {
      BuiltinKit::loadAll(staging);
    }
    status = "builtin kit";
  } else {
    if (WavLoader::loadKit(name.c_str(), staging, status) == 0) {
      Serial.println(String("[kit] ") + status);
      return false;
    }
  }
  Serial.println(String("[kit] ") + status);
  if (engineUp) {
    EngineCommand cmd = {kCmdKitSwap, stagingIdx, 0};
    audioEngine().post(cmd);
  }
  return true;
}

void cmdKit(const String &args) {
  String rest = args;
  String sub = nextWord(rest);
  sub.toLowerCase();
  if (sub.length() == 0) {
    String kits = WavLoader::listKits();
    Serial.println(String("[kit] active=") + activeBank().kitName() +
                   " available: builtin" + (kits.length() ? " " + kits : "") +
                   (WavLoader::sdReady() ? "" : " (no SD)"));
    return;
  }
  if (sub == "builtin") {
    doKitLoad("builtin");
    return;
  }
  if (sub == "load") {
    rest.trim();
    if (rest.length() == 0) {
      Serial.println(F("[kit] usage: kit load <name>"));
      return;
    }
    doKitLoad(rest);
    return;
  }
  Serial.println(F("[kit] usage: kit | kit load <name> | kit builtin"));
}

// --- Backing loops ---
// -1 = no loop. Selecting a loop locks the sequencer's step length to the
// loop's own bar grid, which is the only drift-free way to play over a bed
// whose real tempo is fractional (72.5, 77.6, 65.1 BPM in this pack).
int8_t currentLoop = -1;

void freeRetiredLoop() {
  int16_t *old = audioEngine().takeRetiredLoop();
  if (old != nullptr) {
    free(old);
  }
}

bool selectLoop(int8_t index) {
  if (index < 0) {
    currentLoop = -1;
    seq.setLockedStepFrames(0);  // BPM takes the clock back
    if (audioEngine().running()) {
      EngineCommand cmd = {kCmdLoopClear, 0, 0};
      audioEngine().post(cmd);
    } else {
      freeRetiredLoop();
    }
    Serial.println(F("[loop] off; tempo unlocked"));
    return true;
  }
  if (index >= (int8_t)LoopLibrary::count()) {
    return false;
  }
  const LoopLibrary::LoopInfo &info = LoopLibrary::info((uint8_t)index);
  int16_t *pcm = nullptr;
  uint32_t frames = LoopLibrary::loadLoop((uint8_t)index, &pcm);
  if (frames == 0) {
    Serial.println(String("[loop] failed to load ") + info.name);
    return false;
  }
  uint32_t stepFrames = LoopLibrary::stepFramesFor(info);
  currentLoop = index;
  audioEngine().stageLoop(pcm, frames);
  if (audioEngine().running()) {
    EngineCommand cmd = {kCmdLoopSwap, 0, 0};
    audioEngine().post(cmd);
  } else {
    freeRetiredLoop();  // silent build: nothing plays it, drop it again
  }
  seq.setLockedStepFrames(stepFrames);
  Serial.println(String("[loop] ") + info.title + " " +
                 String(info.bpmTenths / 10.0f, 1) + " BPM, " +
                 String(info.bars) + " bars, " + String(frames * 2 / 1024) +
                 "KB; step=" + String(stepFrames) + " frames (tempo locked)");
  return true;
}

// Loop < > arrows: -1 walks off the front into "no loop".
void uiLoopStep(void *, int8_t dir) {
  uint8_t n = LoopLibrary::count();
  if (n == 0) {
    return;
  }
  int8_t next = currentLoop + dir;
  if (next < -1) next = (int8_t)n - 1;
  if (next >= (int8_t)n) next = -1;
  selectLoop(next);
}

const char *uiLoopName(void *) {
  if (currentLoop < 0) {
    return LoopLibrary::count() ? "-- none --" : "no loops on SD";
  }
  return LoopLibrary::info((uint8_t)currentLoop).title;
}

const char *currentLoopTitle() {
  if (currentLoop < 0) {
    return LoopLibrary::count() ? "-- none --" : "no loops on SD";
  }
  return LoopLibrary::info((uint8_t)currentLoop).title;
}

// Kit < > arrows on the edit panel cycle through builtin + SD kits.
void uiKitStep(void *, int8_t dir) {
  String kits = WavLoader::listKits();
  String names[9];
  uint8_t count = 0;
  names[count++] = "builtin";
  while (kits.length() && count < 9) {
    int space = kits.indexOf(' ');
    if (space < 0) {
      names[count++] = kits;
      kits = "";
    } else {
      names[count++] = kits.substring(0, space);
      kits = kits.substring(space + 1);
    }
  }
  if (count < 2) {
    return;
  }
  uint8_t current = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (names[i] == activeBank().kitName()) {
      current = i;
      break;
    }
  }
  uint8_t next = (current + count + dir) % count;
  doKitLoad(names[next]);
}

void drainEngineEvents() {
  EngineEvent evt;
  while (audioEngine().nextEvent(evt)) {
    switch (evt.type) {
      case kEvtStep:
        flushStepSummary();
        pendingStep = evt.step;
        break;
      case kEvtTrigger: {
        const PadSound &sound = activeBank().pad(evt.pad);
        lastPadIdx = evt.pad;
        voices.trigger(evt.pad + 1, sound.label, evt.vel);
        ui.notePadFlash(evt.pad, evt.vel);
        if (seq.playing() && pendingStep != 0xFF) {
          if (pendingPads.length()) {
            pendingPads += " ";
          }
          pendingPads += String(evt.pad + 1);
        }
        break;
      }
      case kEvtRecorded:
        Serial.println(String("[record] pad ") + String(evt.pad + 1) +
                       " -> step " + String(evt.step + 1));
        break;
      case kEvtKitSwapped:
        onKitSwapped(evt.pad);
        break;
      case kEvtLoopSwapped:
        freeRetiredLoop();  // the engine has stopped reading the old buffer
        break;
      default:
        break;
    }
  }
}

// --- TuneUi callbacks (touch surface) ---

void uiTrigger(void *, uint8_t padIdx, uint8_t velocity) {
  String message = firePad(padIdx, velocity);
  eventLog.add(message);
}

void uiTransport(void *, uint8_t op) {
  switch (op) {
    case TuneUi::kOpPlay:
      transportPlay();
      eventLog.add("Transport play (touch)");
      break;
    case TuneUi::kOpStop:
      transportStop();
      eventLog.add("Transport stop (touch)");
      break;
    case TuneUi::kOpRecordToggle:
      seq.toggleRecord();
      eventLog.add(seq.recording() ? "Record on (touch)" : "Record off (touch)");
      break;
    default:
      break;
  }
}

String uiAudioStatus(void *) {
  if (audioEngine().running()) {
    return String("i2s ") + String(CYPHER_TUNE_ENGINE_RATE / 1000) + "k " +
           String(audioEngine().activeVoices()) + "v u" +
           String(audioEngine().underruns());
  }
  return String("silent (mock)");
}

// Live output taps for the on-screen scope/VU. Returning 0 samples (silent
// build) is what tells the UI to draw the simulated voice meters instead.
uint8_t uiPeak(void *) { return audioEngine().outputPeak(); }

uint16_t uiScope(void *, int16_t *out, uint16_t max) {
  return audioEngine().copyScope(out, max);
}

uint8_t uiVolumeGet(void *) { return audioEngine().masterVolume(); }
void uiVolumeSet(void *, uint8_t v) { audioEngine().setMasterVolume(v); }

// --- Serial commands ---

void cmdStatus(const String &) { printSystemStatus(Serial, "cypher-tune-mpc", storage.eventCount()); }
void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdPad(const String &args) {
  String rest = args;
  uint8_t pad = constrain(nextWord(rest).toInt(), 1, SampleBank::kPadCount);
  rest.trim();
  uint8_t velocity = rest.length() ? constrain(rest.toInt(), 1, 127) : 0;
  String message = firePad(pad - 1, velocity);
  Serial.println(String("[pad] ") + message);
  eventLog.add(message);
}

void cmdGain(const String &args) {
  String rest = args;
  uint8_t pad = constrain(nextWord(rest).toInt(), 1, SampleBank::kPadCount);
  int gain = rest.toInt();
  if (gain < 0 || gain > 255 || !activeBank().setGain(pad - 1, gain)) {
    Serial.println(F("[gain] usage: gain <pad> <0-255>"));
    return;
  }
  Serial.println(String("[gain] pad=") + String(pad) + " gain=" + String(gain));
}

void cmdPitch(const String &args) {
  String rest = args;
  uint8_t pad = constrain(nextWord(rest).toInt(), 1, SampleBank::kPadCount);
  int semis = rest.toInt();
  if (!activeBank().setPitch(pad - 1, semis)) {
    Serial.println(F("[pitch] usage: pitch <pad> <-12..12>"));
    return;
  }
  Serial.println(String("[pitch] pad=") + String(pad) + " semis=" + String(semis));
}

void cmdChoke(const String &args) {
  String rest = args;
  uint8_t pad = constrain(nextWord(rest).toInt(), 1, SampleBank::kPadCount);
  int group = rest.toInt();
  if (group < 0 || group > 4 || !activeBank().setChoke(pad - 1, group)) {
    Serial.println(F("[choke] usage: choke <pad> <0-4> (0 = none)"));
    return;
  }
  Serial.println(String("[choke] pad=") + String(pad) + " group=" + String(group));
}

void cmdStep(const String &args) {
  String rest = args;
  uint8_t step = constrain(nextWord(rest).toInt(), 1, Sequencer::kSteps);
  uint8_t pad = constrain(nextWord(rest).toInt(), 1, Sequencer::kPads);
  rest.trim();
  uint8_t velocity;
  if (rest.length()) {
    velocity = constrain(rest.toInt(), 0, 127);
    seq.setVel(seq.pattern(), step - 1, pad - 1, velocity);
  } else {
    velocity = seq.toggleStep(step - 1, pad - 1);
  }
  Serial.println(String("[step] ") + String(step) + " pad=" + String(pad) +
                 " vel=" + String(velocity) + (velocity ? "" : " (off)"));
}

void cmdVel(const String &args) {
  String rest = args;
  uint8_t step = constrain(nextWord(rest).toInt(), 1, Sequencer::kSteps);
  uint8_t pad = constrain(nextWord(rest).toInt(), 1, Sequencer::kPads);
  uint8_t velocity = constrain(rest.toInt(), 0, 127);
  seq.setVel(seq.pattern(), step - 1, pad - 1, velocity);
  Serial.println(String("[vel] step=") + String(step) + " pad=" + String(pad) +
                 " vel=" + String(velocity));
}

void cmdBpm(const String &args) {
  int next = args.toInt();
  if (!seq.setBpm(next)) {
    Serial.println(F("[bpm] rejected; range is 40-240"));
  }
  Serial.println(String("[bpm] ") + String(seq.bpm()));
}

void cmdSwing(const String &args) {
  int next = args.toInt();
  if (!seq.setSwing(next)) {
    Serial.println(F("[swing] rejected; range is 50-75 (50 = straight)"));
  }
  Serial.println(String("[swing] ") + String(seq.swing()) + "%");
}

void cmdPat(const String &args) {
  String arg = args;
  arg.trim();
  arg.toLowerCase();
  if (arg.length() == 1 && arg[0] >= 'a' && arg[0] <= 'a' + Sequencer::kPatterns - 1) {
    seq.setPattern(arg[0] - 'a');
  } else if (arg.length()) {
    Serial.println(F("[pat] usage: pat <a|b|c|d>"));
  }
  Serial.println(String("[pat] pattern ") + (char)('A' + seq.pattern()) + " " + seq.patternString());
}

void cmdMetro(const String &args) {
  String arg = args;
  arg.trim();
  arg.toLowerCase();
  if (arg == "on") {
    seq.setMetronome(true);
  } else if (arg == "off") {
    seq.setMetronome(false);
  } else if (arg.length() == 0) {
    seq.setMetronome(!seq.metronome());
  } else {
    Serial.println(F("[metro] usage: metro [on|off]"));
  }
  Serial.println(seq.metronome() ? F("[metro] on") : F("[metro] off"));
}

void cmdPlay(const String &) {
  transportPlay();
  Serial.println(String("[play] bpm=") + String(seq.bpm()) + " pattern=" +
                 (char)('A' + seq.pattern()));
  eventLog.add("Transport play");
}

void cmdStop(const String &) {
  transportStop();
  Serial.println(F("[stop] transport stopped"));
  eventLog.add("Transport stop");
}

void cmdRecord(const String &) {
  seq.toggleRecord();
  Serial.println(seq.recording() ? F("[record] on") : F("[record] off"));
}

void cmdPattern(const String &) {
  seq.printPattern(Serial);
}

void cmdSamples(const String &) {
  SampleBank &b = activeBank();
  Serial.println(String("[samples] kit=") + b.kitName() +
                 " loaded=" + String(b.loadedCount()) + "/16 " +
                 String(b.totalBytes() / 1024) + "KB");
  Serial.println(String("[samples] ") + b.sampleMap());
}

void cmdVoices(const String &) {
  Serial.println(String("[voices] ") + voices.summary());
}

void cmdEngine(const String &) {
  Serial.println(String("[engine] ") + audioEngine().statusLine());
}

void cmdSelect(const String &args) {
  uint8_t pad = constrain(args.toInt(), 1, SampleBank::kPadCount);
  ui.selectPad(pad - 1);
  Serial.println(String("[select] pad=") + String(pad) + " " +
                 activeBank().pad(pad - 1).label);
}

void cmdTouch(const String &) {
  Serial.println(String("[touch] ") + ui.touchLine());
}

void cmdPerf(const String &) {
  Serial.println(String("[perf] ") + ui.perfLine());
}

void cmdLoop(const String &args) {
  String arg = args;
  arg.trim();
  if (arg.length() == 0) {
    uint8_t n = LoopLibrary::count();
    Serial.println(String("[loop] active=") + currentLoopTitle() +
                   (seq.locked() ? " (tempo locked)" : "") + " available=" + String(n));
    for (uint8_t i = 0; i < n; i++) {
      const LoopLibrary::LoopInfo &l = LoopLibrary::info(i);
      Serial.println(String("  ") + (i == (uint8_t)currentLoop ? "*" : " ") +
                     l.name + "  " + l.title + "  " +
                     String(l.bpmTenths / 10.0f, 1) + " BPM  " + String(l.bars) +
                     " bars  " + String(l.frames / 22050.0f, 1) + "s");
    }
    if (n == 0) {
      Serial.println(F("  (none; needs /mpc/loops/loops.txt on SD and USE_MPC_SD=1)"));
    }
    return;
  }
  if (arg.equalsIgnoreCase("off") || arg.equalsIgnoreCase("none")) {
    selectLoop(-1);
    return;
  }
  int8_t idx = LoopLibrary::indexOfName(arg.c_str());
  if (idx < 0) {
    Serial.println(String("[loop] no loop named \"") + arg + "\"");
    return;
  }
  selectLoop(idx);
}

void cmdLoopVol(const String &args) {
  String arg = args;
  arg.trim();
  if (arg.length()) {
    audioEngine().setLoopVolume((uint8_t)constrain(arg.toInt(), 0, 255));
  }
  Serial.println(String("[loopvol] ") + String(audioEngine().loopVolume()) + "/255");
}

void cmdBright(const String &args) {
  String arg = args;
  arg.trim();
  if (arg.length()) {
    if (arg == "+" || arg == "up") {
      ui.bumpBrightness(TuneUi::kBrightnessStep);
    } else if (arg == "-" || arg == "down") {
      ui.bumpBrightness(-(int16_t)TuneUi::kBrightnessStep);
    } else {
      ui.setBrightness((uint8_t)constrain(arg.toInt(), 0, 255));
    }
  }
  Serial.println(String("[bright] ") + String(ui.brightness()) + "/255 (min " +
                 String(TuneUi::kMinBrightness) + ")");
}

void cmdVolume(const String &args) {
  String arg = args;
  arg.trim();
  if (arg.length()) {
    audioEngine().setMasterVolume((uint8_t)constrain(arg.toInt(), 0, 255));
  }
  Serial.println(String("[vol] master ") + String(audioEngine().masterVolume()) +
                 "/255");
}

void cmdSettings(const String &args) {
  String arg = args;
  arg.trim();
  arg.toLowerCase();
  if (arg == "on" || arg == "open") {
    ui.setView(TuneUi::kViewSettings);
  } else if (arg == "off" || arg == "close" || arg == "main") {
    ui.setView(TuneUi::kViewMain);
  } else if (arg == "idledim") {
    ui.setIdleDim(!ui.idleDim());
  }
  Serial.println(String("[settings] ") + ui.settingsLine());
}

void cmdTheme(const String &args) {
  String arg = args;
  arg.trim();
  if (arg.length() == 0) {
    Serial.println(String("[theme] ") + ui.themeLine());
    return;
  }
  if (arg.equalsIgnoreCase("next")) {
    ui.cycleTheme();
  } else if (!ui.setThemeByName(arg)) {
    Serial.println(String("[theme] no theme matching \"") + arg + "\"");
    Serial.println(String("[theme] ") + ui.themeLine());
    return;
  }
  Serial.println(String("[theme] ") + ui.themeLine());
}

void onStepFire(void *, uint8_t, uint8_t pad, uint8_t velocity) {
  const PadSound &sound = activeBank().pad(pad);
  lastPadIdx = pad;
  voices.trigger(pad + 1, sound.label, velocity);
  ui.notePadFlash(pad, velocity);
  if (stepFireSummary.length()) {
    stepFireSummary += " ";
  }
  stepFireSummary += String(pad + 1);
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Tune MPC");
  const HardwareProfile &profile = activeHardwareProfile();
  printHardwareProfile(Serial, profile);
  storage.begin("cypher-mpc");
  voices.begin();
  bank.beginDefaults(CYPHER_TUNE_ENGINE_RATE);
  bankStaging.beginDefaults(CYPHER_TUNE_ENGINE_RATE);
  seq.begin(92);
  if (audioEngine().begin(profile, &bank, &bankStaging, &seq, Serial)) {
    uint8_t loaded = BuiltinKit::loadAll(bank);
    Serial.println(String("[engine] builtin kit synthesized: ") +
                   String(loaded) + "/16 pads, " +
                   String(bank.totalBytes() / 1024) + "KB PSRAM");
  }
  TuneUi::Callbacks uiCallbacks;
  uiCallbacks.trigger = uiTrigger;
  uiCallbacks.transport = uiTransport;
  uiCallbacks.audioStatus = uiAudioStatus;
  uiCallbacks.kitStep = uiKitStep;
  uiCallbacks.peak = uiPeak;
  uiCallbacks.scope = uiScope;
  uiCallbacks.volumeGet = uiVolumeGet;
  uiCallbacks.volumeSet = uiVolumeSet;
  if (LoopLibrary::begin() > 0) {
    uiCallbacks.loopStep = uiLoopStep;
    uiCallbacks.loopName = uiLoopName;
  }
  ui.begin(&bank, &seq, &voices, uiCallbacks);
  // Splash runs after the engine so its subtitle reports the real audio state
  // (and after ui.begin(), which owns display bring-up and the saved theme).
  TuneSplash::run(ui.theme(), uiAudioStatus(nullptr).c_str());
  eventLog.add("Cypher Tune MPC booted");
  router.begin(Serial, "mpc");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("pad", "pad <1-16> [vel]", cmdPad);
  router.on("gain", "gain <pad> <0-255>", cmdGain);
  router.on("pitch", "pitch <pad> <-12..12>", cmdPitch);
  router.on("choke", "choke <pad> <0-4>", cmdChoke);
  router.on("step", "step <1-16> <pad> [vel] (no vel = toggle)", cmdStep);
  router.on("vel", "vel <step> <pad> <0-127>", cmdVel);
  router.on("bpm", "bpm <40-240>", cmdBpm);
  router.on("swing", "swing <50-75>", cmdSwing);
  router.on("pat", "pat <a|b|c|d>", cmdPat);
  router.on("metro", "metro [on|off]", cmdMetro);
  router.on("play", "start 16-step transport", cmdPlay);
  router.on("stop", "stop transport", cmdStop);
  router.on("record", "toggle record", cmdRecord);
  router.on("pattern", "print current pattern grid", cmdPattern);
  router.on("samples", "print pad sample map", cmdSamples);
  router.on("voices", "print visual voice state", cmdVoices);
  router.on("engine", "audio engine status", cmdEngine);
  router.on("audio", "alias of engine", cmdEngine);
  router.on("kit", "kit | kit load <name> | kit builtin", cmdKit);
  router.on("select", "select <pad> for the step lane", cmdSelect);
  router.on("touch", "touch tracker diagnostics", cmdTouch);
  router.on("perf", "UI render timing", cmdPerf);
  router.on("theme", "switch UI theme: theme | next | <name>", cmdTheme);
  router.on("loop", "backing loop: loop | <name> | off", cmdLoop);
  router.on("loopvol", "backing loop level: loopvol | <0-255>", cmdLoopVol);
  router.on("bright", "backlight: bright | <40-255> | + | -", cmdBright);
  router.on("vol", "master volume: vol | <0-255>", cmdVolume);
  router.on("settings", "settings view: settings | open | close | idledim", cmdSettings);
}

void loop() {
  if (Serial.available()) {
    ui.noteActivity();  // a serial command counts as use; wake a dimmed panel
  }
  router.poll();
  drainEngineEvents();
  if (!audioEngine().running()) {
    stepFireSummary = "";
    if (seq.tickMillis(millis(), onStepFire, nullptr)) {
      if (stepFireSummary.length()) {
        String event = String("Step ") + String(seq.playStep() + 1) +
                       " pads " + stepFireSummary;
        Serial.println(String("[transport] ") + event);
        eventLog.add(event);
      }
    }
  }
  voices.tick();
  ui.tick();
  delay(2);
}
