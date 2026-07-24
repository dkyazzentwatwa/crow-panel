#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

extern "C" {
#include "src/gnuboy/gnuboy.h"
}

#include "src/GbRomStore.h"
#include "src/GameBoyHost.h"
#include "src/GbVideo.h"
#include "src/GbInput.h"
#include "src/GbUi.h"
#include "src/GbAudio.h"
#include "src/GbTheme.h"
#include "src/GbSplash.h"
#include "src/GbStats.h"

SerialCommandRouter router;
EventLog eventLog;
GbRomStore romStore;
GameBoyHost host;
GbVideo video;
GbInput input;
GbUi ui;
GbAudio audio;
GbStats stats;

enum Screen : uint8_t { kScreenPicker = 0, kScreenPlay };
Screen screen = kScreenPicker;
int8_t selectedRom = 0;
bool paused = false;         // pause overlay open (MENU)
bool fastForward = false;    // run several emulated frames per drawn frame
uint8_t stateSlot = 0;       // selected save-state slot

// Panel backlight 0-255. Floored at kMinBrightness rather than 0: the panel
// keeps rendering at 0 but you cannot see it, which would look exactly like a
// crash and leave no way to find the + button again.
uint8_t brightness = 255;
const uint8_t kMinBrightness = 40;
const uint8_t kStepBrightness = 24;
const uint8_t kStepVolume = 24;

// Idle dimming. Touch or a serial command counts as activity; in a game you
// are touching constantly, so this only really fires when the panel is put down.
uint32_t lastActivityMs = 0;
bool dimmed = false;

// How many emulated frames per drawn frame when fast-forwarding. 3x is a
// noticeable skip through dialogue without becoming unwatchable.
const uint8_t kFastForwardFrames = 3;

// Explicit prototypes: CTAGS_WORKAROUND=1 stubs out ctags, which is what
// normally auto-generates these for .ino files, so anything used before its
// definition must be declared here by hand.
void redrawOverlay();
void refreshPicker();
void applyTheme(GbThemeId id);
void handleOverlay(GbUi::OverlayAction a);
void toPicker();

const char *screenName() { return screen == kScreenPlay ? "play" : "picker"; }

// CrowDisplay does not exist at all on USE_DISPLAY=0 builds (the shared header
// has no stub branch for that case), so every backlight call funnels through
// here rather than sprinkling #if guards through the command handlers.
// Display order: most recently played first, then never-played in card order.
// Kept as an index map so `selectedRom` stays a ROM index while the picker
// works in rows.
uint8_t pickerOrder[GbRomStore::kMaxRoms];
String pickerPlayed[GbRomStore::kMaxRoms];
int8_t selectedRow = 0;

void refreshPicker() {
  const uint8_t n = romStore.count();
  for (uint8_t i = 0; i < n; i++) pickerOrder[i] = i;
  // Insertion sort by rank descending - n is <= 32 and this runs only when the
  // picker is repainted.
  for (uint8_t i = 1; i < n; i++) {
    const uint8_t key = pickerOrder[i];
    const uint32_t kr = stats.rank(romStore.name(key));
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && stats.rank(romStore.name(pickerOrder[j])) < kr) {
      pickerOrder[j + 1] = pickerOrder[j];
      j--;
    }
    pickerOrder[j + 1] = key;
  }
  for (uint8_t i = 0; i < n; i++) {
    pickerPlayed[i] = GbStats::formatPlayed(stats.seconds(romStore.name(pickerOrder[i])));
    if (pickerOrder[i] == (uint8_t)selectedRom) selectedRow = (int8_t)i;
  }
  ui.drawPicker(romStore, selectedRow, pickerOrder, pickerPlayed);
  ui.drawSettings(audio.volume(), brightness, audio.muted());
}

void applyBacklight(uint8_t level) {
#if USE_DISPLAY
  CrowDisplay::setBacklight(level);
#else
  (void)level;
#endif
}

// Themes restyle the chrome only - the emulated screen is never retinted, so
// a game always looks the way the game looks.
void applyTheme(GbThemeId id) {
  GbUi::setTheme(id);
  if (screen == kScreenPicker) {
    refreshPicker();
  } else if (paused) {
    // Repaint the play chrome under the overlay so the whole screen matches.
    ui.drawPlayChrome(romStore.name(selectedRom), !audio.muted(), fastForward);
    redrawOverlay();
  } else {
    ui.drawPlayChrome(romStore.name(selectedRom), !audio.muted(), fastForward);
    video.clearViewport();
  }
}

// Any touch or command resets the idle timer and restores the backlight.
void noteActivity() {
  lastActivityMs = millis();
  if (dimmed) {
    dimmed = false;
    applyBacklight(brightness);
  }
}

void tickIdle() {
  if (dimmed) return;
  if ((uint32_t)(millis() - lastActivityMs) < GB_IDLE_DIM_MS) return;
  dimmed = true;
  applyBacklight(kMinBrightness);
}

void applyBrightness(int delta) {
  int v = (int)brightness + delta;
  if (v > 255) v = 255;
  if (v < (int)kMinBrightness) v = kMinBrightness;
  brightness = (uint8_t)v;
  applyBacklight(brightness);
}

void applyVolume(int delta) {
  int v = (int)audio.volume() + delta;
  if (v > 255) v = 255;
  if (v < 0) v = 0;
  audio.setVolume((uint8_t)v);
  // Nudging the volume up off zero is an obvious "I want sound" gesture.
  if (delta > 0 && audio.muted()) audio.setMuted(false);
}

// Leave the running cartridge: flush the battery save first, then repaint the
// picker. Both the MENU control and the `screen picker` command land here, so
// touch and serial behave identically.
void toPicker() {
  if (host.sramDirty()) host.save();
  stats.endSession(millis());
  screen = kScreenPicker;
  paused = false;
  refreshPicker();
  eventLog.add("Returned to picker");
}

bool launchRom(int8_t index) {
  if (index < 0 || index >= (int8_t)romStore.count()) return false;
  if (!romStore.ready()) {
    Serial.println(F("[play] placeholder list - no real ROM to load (need -DUSE_GB_SD=1 + card)"));
    return false;
  }
  if (!host.loadRom(romStore.romPath(index), romStore.savePath(index))) {
    Serial.print(F("[play] load failed: "));
    Serial.println(host.status());
    return false;
  }
  selectedRom = index;
  stats.startSession(romStore.name(index), millis());
  screen = kScreenPlay;
  paused = false;
  ui.drawPlayChrome(romStore.name(index), !audio.muted(), fastForward);
  video.clearViewport();
  eventLog.add(String("Launched ") + romStore.name(index));
  return true;
}

void cmdStatus(const String &) {
  printSystemStatus(Serial, "cypher-boy", 0);
  Serial.print(F("[status] flags USE_DISPLAY="));
  Serial.print(USE_DISPLAY);
  Serial.print(F(" USE_GB_SD="));
  Serial.println(USE_GB_SD);
}

void cmdHistory(const String &) {
  eventLog.printHistory(Serial);
}

void cmdRom(const String &) {
  Serial.print(F("[rom] source="));
  Serial.println(romStore.status());
  for (uint8_t i = 0; i < romStore.count(); i++) {
    Serial.print(F("  "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(romStore.name(i));
    Serial.print(F("  -> "));
    Serial.print(romStore.romPath(i));
    Serial.print(F("  save="));
    Serial.println(romStore.savePath(i));
  }
  if (!romStore.ready()) {
    Serial.println(F("[rom] placeholder list - build with -DUSE_GB_SD=1 and insert a card"));
  }
}

// `selftest` - drive the mock flow headlessly with explicit PASS/FAIL.
// Assertions are on real state; anything that genuinely needs a card or a ROM
// is reported as SKIP rather than a fake PASS.
void cmdSelfTest(const String &) {
  Serial.println(F("[selftest] Cypher Boy"));
  int pass = 0, fail = 0, skip = 0;
  auto check = [&](const char *name, bool ok) {
    Serial.print(ok ? F("[selftest] PASS ") : F("[selftest] FAIL "));
    Serial.println(name);
    ok ? pass++ : fail++;
  };
  auto skipped = [&](const char *name, const char *why) {
    Serial.print(F("[selftest] SKIP "));
    Serial.print(name);
    Serial.print(F(" - "));
    Serial.println(why);
    skip++;
  };

  // 1) Emulator core comes up and allocates its buffers.
  check("host begin", host.begin(&audio));
  check("framebuffer allocated", host.framebuffer() != nullptr);

  // 1b) The EmuCore seam. The launcher will pick a console through this
  // interface, so it has to report the right thing polymorphically - not just
  // compile.
  {
    EmuCore *core = &host;
    check("core reports its system", core->system() == kSysGameBoy);
    check("core names itself", core->name() != nullptr && core->name()[0] != 0);
    check("core reports native size",
          core->frameW() == GB_W && core->frameH() == GB_H && core->scale() == GB_SCALE);
    check("core framebuffer matches host", core->framebuffer() == host.framebuffer());
  }

  // 2) Running with no ROM loaded must be a safe no-op, not a crash.
  uint32_t before = host.frameCount();
  host.runFrame(false);
  check("runFrame is a no-op with no ROM", host.frameCount() == before);

  // 3) ROM store reports a usable list in whichever mode it is in.
  check("rom store has entries", romStore.count() > 0);
  check("save path derives from rom name",
        romStore.savePath(0).endsWith(".sav") &&
            romStore.savePath(0).startsWith(GB_SAVE_DIR));

  // Guard the two path namespaces. The Arduino FS API prepends the mount point
  // itself, so an FS path that already contains it resolves to
  // "/sdcard/sdcard/..." and fails on a perfectly good card - which is exactly
  // the bug that made a working SD report "NO SD".
  check("FS paths are mount-relative",
        String(GB_ROM_DIR_FS)[0] == '/' &&
            !String(GB_ROM_DIR_FS).startsWith(GB_SD_ROOT) &&
            !String(GB_SAVE_DIR_FS).startsWith(GB_SD_ROOT));
  check("stdio paths are mount-prefixed",
        String(GB_ROM_DIR) == String(GB_SD_ROOT) + GB_ROM_DIR_FS &&
            String(GB_SAVE_DIR) == String(GB_SD_ROOT) + GB_SAVE_DIR_FS);

  // 4) Video scaling maths (pure - verifiable with no panel attached).
  check("viewport origin", GbVideo::viewX(0) == GB_VIEW_X && GbVideo::viewY(0) == GB_VIEW_Y);
  check("viewport scales x3",
        GbVideo::viewX(GB_W - 1) == GB_VIEW_X + (GB_W - 1) * GB_SCALE &&
            GbVideo::viewY(GB_H - 1) == GB_VIEW_Y + (GB_H - 1) * GB_SCALE);
  check("viewport size", GbVideo::viewW() == GB_W * GB_SCALE &&
                             GbVideo::viewH() == GB_H * GB_SCALE);
  check("viewport fits panel", GB_VIEW_X + GbVideo::viewW() <= 1024 &&
                                  GB_VIEW_Y + GbVideo::viewH() <= 600);

  // 5) Every gamepad hitbox maps to its own button, and nothing else does.
  {
    uint8_t n = 0;
    const GbHitbox *L = GbInput::layout(n);
    bool allMap = (n > 0);
    bool noOverlap = true;
    for (uint8_t i = 0; i < n; i++) {
      if (L[i].bit == 0) continue;  // MENU is edge-triggered, not a pad bit
      const uint32_t got = input.mapPoint(L[i].x + L[i].w / 2, L[i].y + L[i].h / 2);
      if ((got & L[i].bit) == 0) allMap = false;
      if (got != L[i].bit) noOverlap = false;  // centre must hit exactly one control
    }
    check("every control maps to its button", allMap);
    check("controls do not overlap", noOverlap);
    check("empty space maps to nothing", input.mapPoint(GB_VIEW_X + 10, GB_VIEW_Y + 10) == 0);

    // Controls must not sit on top of the game picture.
    bool clearOfViewport = true;
    for (uint8_t i = 0; i < n; i++) {
      if (L[i].x < GB_VIEW_X + GbVideo::viewW() && L[i].y < GB_VIEW_Y + GbVideo::viewH() &&
          L[i].x + L[i].w > GB_VIEW_X && L[i].y + L[i].h > GB_VIEW_Y) {
        clearOfViewport = false;
      }
    }
    check("controls clear of viewport", clearOfViewport);

    // Every control must fit on the 1024x600 panel. Without this, an
    // off-screen button is invisible AND untappable, and nothing else catches
    // it until the panel is in your hands.
    bool onPanel = true;
    for (uint8_t i = 0; i < n; i++) {
      if (L[i].x < 0 || L[i].y < 0 || L[i].x + L[i].w > 1024 || L[i].y + L[i].h > 600) {
        onPanel = false;
        Serial.print(F("[selftest]   off-panel control: "));
        Serial.println(L[i].label);
      }
    }
    check("all controls fit on panel", onPanel);
  }

  // 6) ROM picker row hit-testing (pure).
  check("picker hits first row",
        GbUi::pickerHit(GbUi::kRowX + 10, GbUi::kRowTop + 4, romStore.count()) == 0);
  check("picker misses header", GbUi::pickerHit(GbUi::kRowX + 10, 20, romStore.count()) < 0);
  check("picker misses past last row",
        GbUi::pickerHit(GbUi::kRowX + 10,
                        GbUi::kRowTop + romStore.count() * GbUi::kRowH + 4,
                        romStore.count()) < 0);

  // 7) Pause-overlay hit-testing (pure, verifiable with no panel).
  {
    struct OvCase { GbUi::OverlayAction want; const char *name; };
    // Centres of the six overlay buttons must each resolve to their action.
    const int16_t c1 = 250 + 30 + 110, c2 = 250 + 270 + 110;
    const int16_t r1 = 130 + 96 + 27, r2 = 130 + 162 + 27, r3 = 130 + 228 + 27;
    bool ovOk = GbUi::overlayHit(c1, r1) == GbUi::kOvResume &&
                GbUi::overlayHit(c2, r1) == GbUi::kOvQuit &&
                GbUi::overlayHit(c1, r2) == GbUi::kOvSaveState &&
                GbUi::overlayHit(c2, r2) == GbUi::kOvLoadState &&
                GbUi::overlayHit(c1, r3) == GbUi::kOvToggleFF &&
                GbUi::overlayHit(c2, r3) == GbUi::kOvToggleSound;
    check("overlay buttons map to actions", ovOk);
    check("overlay ignores empty space", GbUi::overlayHit(260, 140) == GbUi::kOvNone);

    bool slotOk = true;
    for (uint8_t i = 0; i < GB_STATE_SLOTS; i++) {
      if (GbUi::overlaySlotHit(250 + 220 + i * 70 + 30, 130 + 56 + 17) != i) slotOk = false;
    }
    check("overlay slot pills map to slots", slotOk);
    check("save-state path uses the slot",
          romStore.statePath(0, 1).endsWith(".st1"));
  }

  // 8) Settings steppers: minus/plus hit-test, and the brightness floor.
  {
    const int16_t bx = GbUi::kSetX, bw = GbUi::kSetW;
    const int16_t vy = GbUi::volRowY();
    check("stepper minus zone",
          GbUi::stepperHit(bx + 10, vy + 40, bx, vy, bw) == GbUi::kStepMinus);
    check("stepper plus zone",
          GbUi::stepperHit(bx + bw - 10, vy + 40, bx, vy, bw) == GbUi::kStepPlus);
    check("stepper bar is not a button",
          GbUi::stepperHit(bx + bw / 2, vy + 40, bx, vy, bw) == GbUi::kStepNone);
    check("stepper ignores the label line",
          GbUi::stepperHit(bx + 10, vy + 2, bx, vy, bw) == GbUi::kStepNone);
    check("overlay has its own vol/bright steppers",
          GbUi::overlayVolHit(0, 0) == GbUi::kStepNone &&
              GbUi::overlayBrightHit(0, 0) == GbUi::kStepNone);

    // Brightness must never reach 0: the panel keeps rendering but goes black,
    // which is indistinguishable from a crash and hides the + button.
    const uint8_t before = brightness;
    for (int i = 0; i < 40; i++) applyBrightness(-kStepBrightness);
    check("brightness floors above black", brightness >= kMinBrightness);
    brightness = before;
    applyBacklight(brightness);
  }

  // 9) Themes: cycling wraps, names resolve, and every palette is populated.
  {
    const GbThemeId first = GbUi::theme();
    GbThemeId t = first;
    for (uint8_t i = 0; i < kGbThemeCount; i++) t = nextGbTheme(t);
    check("theme cycle wraps to start", t == first);
    check("prev is the inverse of next", prevGbTheme(nextGbTheme(first)) == first);
    check("theme resolves by name",
          gbThemeFromName(gbTheme(kGbThemeDmgGreen).name) == kGbThemeDmgGreen);
    check("unknown theme name falls back", gbThemeFromName("nonsense") == kGbThemeOpsTeal);

    bool named = true, distinct = true;
    for (uint8_t i = 0; i < kGbThemeCount; i++) {
      const GbPalette &p = gbTheme((GbThemeId)i);
      if (!p.name || !p.name[0]) named = false;
      // A palette whose text matches its background is invisible - catch a
      // half-filled theme here rather than on a black screen.
      if (p.ink == p.bg || p.accent == p.bg) distinct = false;
    }
    check("every theme is named", named);
    check("no theme has invisible text", distinct);
    check("theme row hit-tests",
          GbUi::themeRowHit(GbUi::kSetX + 10, GbUi::themeRowY() + 10) &&
              !GbUi::themeRowHit(10, 10));
  }

  // 10) Actually booting a cartridge needs a real card + ROM file.
  if (romStore.ready() && romStore.count() > 0) {
    bool loaded = host.loadRom(romStore.romPath(0), romStore.savePath(0));
    check("rom loads from SD", loaded);
    if (loaded) {
      uint32_t f0 = host.frameCount();
      for (int i = 0; i < 10; i++) host.runFrame(false);
      check("frames advance", host.frameCount() == f0 + 10);
    }
  } else {
    skipped("rom loads from SD", "no SD card mounted (build -DUSE_GB_SD=1 with a card)");
    skipped("frames advance", "requires a loaded ROM");
  }

  Serial.print(F("[selftest] "));
  Serial.print(pass);
  Serial.print(F(" passed, "));
  Serial.print(fail);
  Serial.print(F(" failed, "));
  Serial.print(skip);
  Serial.println(F(" skipped"));
  eventLog.add(fail == 0 ? "Selftest PASS" : "Selftest FAIL");
}

void cmdScreen(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  if (a.startsWith("play")) {
    String rest = a.substring(4);
    rest.trim();
    int8_t idx = rest.length() ? (int8_t)rest.toInt() : selectedRom;
    if (!launchRom(idx)) Serial.println(F("[screen] launch failed"));
  } else if (a.startsWith("picker") || a.startsWith("menu")) {
    toPicker();
  }
  Serial.print(F("[screen] "));
  Serial.println(screenName());
}

void cmdPlay(const String &args) { cmdScreen(String("play ") + args); }

void cmdTouch(const String &) {
  Serial.print(F("[touch] raw="));
  Serial.print(input.rawX());
  Serial.print(F(","));
  Serial.print(input.rawY());
  Serial.print(F(" mapped="));
  Serial.print(input.x());
  Serial.print(F(","));
  Serial.print(input.y());
  Serial.print(F(" down="));
  Serial.print(input.down() ? F("yes") : F("no"));
  Serial.print(F(" taps="));
  Serial.print(input.tapCount());
  Serial.print(F(" buttons=0x"));
  Serial.print(input.buttons(), HEX);
  Serial.print(F(" screen="));
  Serial.println(screenName());
}

// Serial twin of the touch gamepad: `button a`, `button up+a`, `button none`.
void cmdButton(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  uint32_t bits = 0;
  if (a.indexOf("up") >= 0) bits |= GB_BTN_UP;
  if (a.indexOf("down") >= 0) bits |= GB_BTN_DOWN;
  if (a.indexOf("left") >= 0) bits |= GB_BTN_LEFT;
  if (a.indexOf("right") >= 0) bits |= GB_BTN_RIGHT;
  if (a.indexOf("select") >= 0) bits |= GB_BTN_SELECT;
  if (a.indexOf("start") >= 0) bits |= GB_BTN_START;
  // Check 'a'/'b' as standalone tokens so "start" doesn't set A.
  if (a == "a" || a.indexOf("a+") >= 0 || a.indexOf("+a") >= 0) bits |= GB_BTN_A;
  if (a == "b" || a.indexOf("b+") >= 0 || a.indexOf("+b") >= 0) bits |= GB_BTN_B;
  input.injectButtons(bits);
  Serial.print(F("[button] injected 0x"));
  Serial.println(bits, HEX);
}

void cmdAudio(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  if (a.startsWith("mute")) {
    audio.setMuted(true);
  } else if (a.startsWith("unmute")) {
    audio.setMuted(false);
  } else if (a.startsWith("vol")) {
    String v = a.substring(3);
    v.trim();
    if (v.length()) audio.setVolume((uint8_t)constrain(v.toInt(), 0, 255));
  }
  Serial.print(F("[audio] "));
  Serial.print(audio.status());
  Serial.print(F(" vol="));
  Serial.print(audio.volume());
  Serial.print(F(" muted="));
  Serial.print(audio.muted() ? F("yes") : F("no"));
  Serial.print(F(" underruns="));
  Serial.println(audio.underruns());
  if (screen == kScreenPicker) ui.drawSettings(audio.volume(), brightness, audio.muted());
  else if (paused) redrawOverlay();
}

void cmdTheme(const String &args) {
  String a = args;
  a.trim();
  if (a.length() == 0 || a.equalsIgnoreCase("next")) {
    applyTheme(nextGbTheme(GbUi::theme()));
  } else if (a.equalsIgnoreCase("prev")) {
    applyTheme(prevGbTheme(GbUi::theme()));
  } else if (a.equalsIgnoreCase("list")) {
    for (uint8_t i = 0; i < kGbThemeCount; i++) {
      Serial.print(F("  "));
      Serial.print(i);
      Serial.print(F(": "));
      Serial.println(gbTheme((GbThemeId)i).name);
    }
  } else {
    applyTheme(gbThemeFromName(a));
  }
  Serial.print(F("[theme] "));
  Serial.println(gbTheme(GbUi::theme()).name);
}

void cmdStats(const String &) {
  Serial.println(F("[stats] play time by ROM"));
  for (uint8_t i = 0; i < romStore.count(); i++) {
    const String &n = romStore.name(i);
    Serial.print(F("  "));
    Serial.print(n);
    Serial.print(F("  "));
    Serial.print(GbStats::formatPlayed(stats.seconds(n)));
    Serial.print(F("  rank="));
    Serial.println(stats.rank(n));
  }
}

void cmdBright(const String &args) {
  String a = args;
  a.trim();
  if (a.length()) {
    int v = a.toInt();
    brightness = (uint8_t)constrain(v, (int)kMinBrightness, 255);
    applyBacklight(brightness);
  }
  Serial.print(F("[bright] backlight="));
  Serial.print(brightness);
  Serial.print(F(" ("));
  Serial.print((brightness * 100 + 127) / 255);
  Serial.println(F("%)"));
  if (screen == kScreenPicker) ui.drawSettings(audio.volume(), brightness, audio.muted());
  else if (paused) redrawOverlay();
}

void cmdSave(const String &) {
  if (!host.romLoaded()) {
    Serial.println(F("[save] no ROM loaded"));
    return;
  }
  Serial.println(host.save() ? F("[save] battery SRAM written")
                             : F("[save] write FAILED (see log)"));
}

void redrawOverlay() {
  ui.drawPauseOverlay(stateSlot, !audio.muted(), fastForward, audio.volume(), brightness);
}

void handleOverlay(GbUi::OverlayAction a) {
  switch (a) {
    case GbUi::kOvResume:
      paused = false;
      ui.drawPlayChrome(romStore.name(selectedRom), !audio.muted(), fastForward);
      video.clearViewport();
      break;
    case GbUi::kOvSaveState:
      host.saveState(romStore.statePath(selectedRom, stateSlot));
      host.saveThumb(romStore.statePath(selectedRom, stateSlot));
      eventLog.add(String("Saved state slot ") + stateSlot);
      redrawOverlay();
      break;
    case GbUi::kOvLoadState:
      host.loadState(romStore.statePath(selectedRom, stateSlot));
      eventLog.add(String("Loaded state slot ") + stateSlot);
      paused = false;
      ui.drawPlayChrome(romStore.name(selectedRom), !audio.muted(), fastForward);
      video.clearViewport();
      break;
    case GbUi::kOvToggleFF:
      fastForward = !fastForward;
      redrawOverlay();
      break;
    case GbUi::kOvToggleSound:
      audio.setMuted(!audio.muted());
      redrawOverlay();
      break;
    case GbUi::kOvNextTheme:
      applyTheme(nextGbTheme(GbUi::theme()));
      break;
    case GbUi::kOvQuit:
      toPicker();
      break;
    case GbUi::kOvNone:
    default:
      break;
  }
}

void cmdState(const String &args) {
  String a = args;
  a.trim();
  a.toLowerCase();
  int sp = a.indexOf(' ');
  String verb = sp > 0 ? a.substring(0, sp) : a;
  String rest = sp > 0 ? a.substring(sp + 1) : String();
  rest.trim();
  if (rest.length()) stateSlot = (uint8_t)constrain(rest.toInt(), 0, GB_STATE_SLOTS - 1);

  if (verb == "save") {
    const bool ok = host.saveState(romStore.statePath(selectedRom, stateSlot));
    if (ok) host.saveThumb(romStore.statePath(selectedRom, stateSlot));
    Serial.println(ok ? F("[state] saved") : F("[state] save FAILED"));
  } else if (verb == "load") {
    Serial.println(host.loadState(romStore.statePath(selectedRom, stateSlot))
                       ? F("[state] loaded") : F("[state] load FAILED (no such slot?)"));
  }
  Serial.print(F("[state] slot="));
  Serial.print(stateSlot);
  Serial.print(F(" path="));
  Serial.println(romStore.statePath(selectedRom, stateSlot));
}

void cmdFF(const String &) {
  fastForward = !fastForward;
  Serial.print(F("[ff] fast-forward "));
  Serial.println(fastForward ? F("ON") : F("OFF"));
  if (screen == kScreenPlay && !paused) {
    ui.drawPlayChrome(romStore.name(selectedRom), !audio.muted(), fastForward);
    video.clearViewport();
  }
}

void cmdPause(const String &) {
  if (screen != kScreenPlay) { Serial.println(F("[pause] not in a game")); return; }
  paused = !paused;
  if (paused) redrawOverlay();
  else { ui.drawPlayChrome(romStore.name(selectedRom), !audio.muted(), fastForward); video.clearViewport(); }
  Serial.println(paused ? F("[pause] paused") : F("[pause] resumed"));
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Boy (Game Boy / GBC player)");
  printHardwareProfile(Serial, activeHardwareProfile());

  audio.begin();
  Logger::info("gbaudio", audio.status());
  host.begin(&audio);
  Logger::info("gb", host.status());

  romStore.begin();
  Logger::info("romstore", romStore.status());

  ui.begin();
  GbInput::buildLayout(GB_VIEW_X, GB_VIEW_Y, GbVideo::viewW(), GbVideo::viewH());
  ui.setStateSource(&romStore, &selectedRom);
  stats.begin();
  video.begin();
  applyBacklight(brightness);
  GbSplash::run(&audio, &input);
  noteActivity();
  refreshPicker();

  eventLog.add("Cypher Boy booted");
  router.begin(Serial, "cypher-boy");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("rom", "list ROMs found on SD", cmdRom);
  router.on("play", "launch a ROM by index: play 0", cmdPlay);
  router.on("screen", "picker | play [n]", cmdScreen);
  router.on("button", "inject a pad press: button up+a | button none", cmdButton);
  router.on("save", "force-write battery SRAM to SD now", cmdSave);
  router.on("audio", "audio status | vol <0-255> | mute | unmute", cmdAudio);
  router.on("bright", "backlight: bright <40-255>", cmdBright);
  router.on("theme", "theme next|prev|list|<name>", cmdTheme);
  router.on("stats", "play time and recency per ROM", cmdStats);
  router.on("state", "state save|load [slot] - save-state slots", cmdState);
  router.on("ff", "toggle fast-forward", cmdFF);
  router.on("pause", "toggle the in-game pause overlay", cmdPause);
  router.on("touch", "print raw + mapped touch, taps, buttons, screen", cmdTouch);
  router.on("selftest", "drive the mock flow headlessly with PASS/FAIL", cmdSelfTest);
}

void loop() {
  router.poll();
  input.tick();
  if (input.releasedEdge() || input.pressedEdge()) noteActivity();
  tickIdle();

  if (screen == kScreenPlay) {
    if (paused) {
      // Overlay is modal: gameplay is frozen and only overlay taps count.
      if (input.releasedEdge()) {
        const int16_t tx = input.releaseX(), ty = input.releaseY();
        const GbUi::StepHit sv = GbUi::overlayVolHit(tx, ty);
        const GbUi::StepHit sb = GbUi::overlayBrightHit(tx, ty);
        const uint8_t slot = GbUi::overlaySlotHit(tx, ty);
        if (sv != GbUi::kStepNone) {
          applyVolume(sv * kStepVolume);
          redrawOverlay();
        } else if (sb != GbUi::kStepNone) {
          applyBrightness(sb * kStepBrightness);
          redrawOverlay();
        } else if (slot != 0xFF) {
          stateSlot = slot;
          redrawOverlay();
        } else {
          handleOverlay(GbUi::overlayHit(tx, ty));
        }
      }
      delay(5);
      return;
    }

    if (input.menuPressed()) {
      // MENU opens the pause menu rather than quitting outright, so a stray
      // tap can never dump you out of a game.
      paused = true;
      if (host.sramDirty()) host.save();
      redrawOverlay();
      delay(5);
      return;
    }

    const uint32_t pad = input.buttons();
    host.setPad(pad);
    // Fast-forward runs extra frames without drawing them - the emulation is
    // cheap, the blit is what costs.
    const uint8_t frames = fastForward ? kFastForwardFrames : 1;
    for (uint8_t i = 0; i < frames; i++) {
      host.runFrame(i == (uint8_t)(frames - 1));
    }
    video.blit(host.framebuffer());
    ui.drawButtonState(pad);
    host.tickSave(millis());
    // No delay: the emulator paces itself on the GB frame loop (and on audio
    // when sound is enabled), and touch/serial are polled once per frame.
    return;
  }

  // Picker: releasing on a ROM row launches it. Release-edge (not held) so a
  // drag that starts on one row and ends on another launches neither.
  if (input.releasedEdge()) {
    const int16_t tx = input.releaseX(), ty = input.releaseY();

    // Settings steppers first - they sit in the right column, clear of the
    // ROM rows, but check them before treating the tap as a launch.
    GbUi::StepHit sv = GbUi::stepperHit(tx, ty, GbUi::kSetX, GbUi::volRowY(), GbUi::kSetW);
    GbUi::StepHit sb = GbUi::stepperHit(tx, ty, GbUi::kSetX, GbUi::brightRowY(), GbUi::kSetW);
    if (sv != GbUi::kStepNone) {
      audio.playClick();
      applyVolume(sv * kStepVolume);
      ui.drawSettings(audio.volume(), brightness, audio.muted());
      delay(5);
      return;
    }
    if (sb != GbUi::kStepNone) {
      audio.playClick();
      applyBrightness(sb * kStepBrightness);
      ui.drawSettings(audio.volume(), brightness, audio.muted());
      delay(5);
      return;
    }
    if (GbUi::themeRowHit(tx, ty)) {
      audio.playClick();
      applyTheme(nextGbTheme(GbUi::theme()));
      delay(5);
      return;
    }

    const int8_t row = GbUi::pickerHit(tx, ty, romStore.count());
    if (row >= 0) {
      audio.playClick();
      selectedRow = row;
      const uint8_t romIdx = pickerOrder[row];
      selectedRom = (int8_t)romIdx;
      if (!launchRom(romIdx)) {
        // Still show the selection so the tap is not silently ignored.
        refreshPicker();
      }
    }
  }
  delay(5);
}
