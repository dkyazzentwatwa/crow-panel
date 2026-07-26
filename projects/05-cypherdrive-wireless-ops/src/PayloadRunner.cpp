#include "PayloadRunner.h"
#include <CrowPanelShared.h>

namespace {
// Benign, operator-chosen automation presets in DuckyScript. No credential/
// exfiltration/persistence/network-attack payloads - those are out of scope.
struct Preset {
  const char *name;
  const char *script;
};
const Preset kPresets[] = {
    {"Open Terminal",
     "GUI SPACE\nDELAY 400\nSTRING Terminal\nDELAY 300\nENTER\n"},
    {"Hello World",
     "GUI SPACE\nDELAY 400\nSTRING Terminal\nDELAY 300\nENTER\nDELAY 1200\n"
     "STRING echo 'Hello from CypherDrive'\nENTER\n"},
    {"Open Apps",
     "GUI SPACE\nDELAY 400\nSTRING Terminal\nENTER\nDELAY 1000\n"
     "STRING open -a \"Safari\"\nENTER\nDELAY 800\n"
     "STRING open -a \"Calendar\"\nENTER\n"},
    {"Notes Demo",
     "STRING CypherDrive HID demo - benign sample text.\nENTER\n"
     "STRING This is safe text for notes, chat, or docs.\nENTER\nSTRING Done.\n"},
    {"Lock Screen", "CTRL GUI q\n"},
    {"Screenshot", "GUI SHIFT 4\n"},
};
const uint8_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

String firstToken(const String &s) {
  int sp = s.indexOf(' ');
  return sp < 0 ? s : s.substring(0, sp);
}
String restAfter(const String &s) {
  int sp = s.indexOf(' ');
  return sp < 0 ? String("") : s.substring(sp + 1);
}

uint8_t modBit(const String &up) {
  if (up == "GUI" || up == "WINDOWS" || up == "WIN" || up == "COMMAND" || up == "CMD" ||
      up == "META")
    return kModCmd;
  if (up == "CTRL" || up == "CONTROL") return kModCtrl;
  if (up == "ALT" || up == "OPTION" || up == "OPT") return kModOpt;
  if (up == "SHIFT") return kModShift;
  return 0;
}

uint8_t keyForToken(const String &tk) {
  if (tk.length() == 1) return (uint8_t)tk[0];
  String up = tk;
  up.toUpperCase();
  if (up == "ENTER" || up == "RETURN") return kKeyReturn;
  if (up == "SPACE") return ' ';
  if (up == "TAB") return kKeyTab;
  if (up == "ESC" || up == "ESCAPE") return kKeyEsc;
  if (up == "BACKSPACE" || up == "DEL" || up == "DELETE") return kKeyBackspace;
  if (up == "UP" || up == "UPARROW") return kKeyUpArrow;
  if (up == "DOWN" || up == "DOWNARROW") return kKeyDownArrow;
  if (up == "LEFT" || up == "LEFTARROW") return kKeyLeftArrow;
  if (up == "RIGHT" || up == "RIGHTARROW") return kKeyRightArrow;
  if (up.length() >= 2 && up[0] == 'F') {
    int n = up.substring(1).toInt();
    if (n >= 1 && n <= 12) return (uint8_t)(kKeyF1 + (n - 1));
  }
  return 0;
}
}  // namespace

void PayloadRunner::begin(HidPad *hid, ScanLog *log) {
  hid_ = hid;
  log_ = log;
}

uint8_t PayloadRunner::presetCount() const { return kPresetCount; }
const char *PayloadRunner::presetName(uint8_t index) const {
  return index < kPresetCount ? kPresets[index].name : "";
}
const char *PayloadRunner::presetScript(uint8_t index) const {
  return index < kPresetCount ? kPresets[index].script : "";
}

void PayloadRunner::run(const String &name, const String &script) {
  script_ = script;
  name_ = name;
  pos_ = 0;
  total_ = script_.length();
  defaultDelayMs_ = 0;
  running_ = total_ > 0;
  nextMs_ = millis();
  if (log_) log_->recordHid(String("payload ") + name_, "started");
}

void PayloadRunner::stop() {
  if (running_ && log_) log_->recordHid(String("payload ") + name_, "stopped");
  running_ = false;
}

uint8_t PayloadRunner::progressPct() const {
  if (total_ == 0) return 0;
  uint32_t p = (uint32_t)pos_ * 100 / total_;
  return p > 100 ? 100 : (uint8_t)p;
}

void PayloadRunner::processLine(const String &line) {
  if (line.length() == 0) return;
  String ft = firstToken(line);
  String up = ft;
  up.toUpperCase();
  if (up == "REM" || up == "ID" || up.startsWith("//")) return;
  if (up == "STRING") {
    if (hid_) hid_->typeText(restAfter(line));
    return;
  }
  if (up == "STRINGLN") {
    if (hid_) {
      hid_->typeText(restAfter(line));
      hid_->tapKey(0, kKeyReturn);
    }
    return;
  }
  // Otherwise: a named key and/or a modifier combo (e.g. "GUI SPACE",
  // "CTRL ALT t", "ENTER"). Leading modifier tokens accumulate; the last
  // non-modifier token is the key.
  uint8_t mods = 0;
  uint8_t key = 0;
  String rest = line;
  rest.trim();
  while (rest.length() > 0) {
    String tk = firstToken(rest);
    rest = restAfter(rest);
    rest.trim();
    String tkUp = tk;
    tkUp.toUpperCase();
    uint8_t mb = modBit(tkUp);
    if (mb) {
      mods |= mb;
    } else {
      uint8_t k = keyForToken(tk);
      if (k) key = k;
    }
  }
  if ((mods || key) && hid_) hid_->tapKey(mods, key);
}

void PayloadRunner::service(uint32_t nowMs) {
  if (!running_) return;
  if ((int32_t)(nowMs - nextMs_) < 0) return;
  if (pos_ >= script_.length()) {
    running_ = false;
    if (log_) log_->recordHid(String("payload ") + name_, "done");
    return;
  }
  int nl = script_.indexOf('\n', pos_);
  String line = (nl < 0) ? script_.substring(pos_) : script_.substring(pos_, nl);
  pos_ = (nl < 0) ? script_.length() : (uint16_t)(nl + 1);
  line.replace("\r", "");
  line.trim();

  uint32_t gap = defaultDelayMs_ > 0 ? defaultDelayMs_ : 30;
  String up = firstToken(line);
  up.toUpperCase();
  if (up == "DELAY") {
    gap = (uint32_t)restAfter(line).toInt();
  } else if (up == "DEFAULT_DELAY" || up == "DEFAULTDELAY") {
    defaultDelayMs_ = (uint16_t)restAfter(line).toInt();
  } else {
    processLine(line);
  }
  nextMs_ = nowMs + gap;
}
