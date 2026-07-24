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

SerialCommandRouter router;
EventLog eventLog;
GbRomStore romStore;
GameBoyHost host;
GbVideo video;
GbInput input;
GbUi ui;

enum Screen : uint8_t { kScreenPicker = 0, kScreenPlay };
Screen screen = kScreenPicker;
int8_t selectedRom = 0;

const char *screenName() { return screen == kScreenPlay ? "play" : "picker"; }

// Leave the running cartridge: flush the battery save first, then repaint the
// picker. Both the MENU control and the `screen picker` command land here, so
// touch and serial behave identically.
void toPicker() {
  if (host.sramDirty()) host.save();
  screen = kScreenPicker;
  ui.drawPicker(romStore, selectedRom);
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
  screen = kScreenPlay;
  ui.drawPlayChrome(romStore.name(index));
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
  check("host begin", host.begin());
  check("framebuffer allocated", host.framebuffer() != nullptr);

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

  // 7) Actually booting a cartridge needs a real card + ROM file.
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

void cmdSave(const String &) {
  if (!host.romLoaded()) {
    Serial.println(F("[save] no ROM loaded"));
    return;
  }
  Serial.println(host.save() ? F("[save] battery SRAM written")
                             : F("[save] write FAILED (see log)"));
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "CrowPanel Cypher Boy (Game Boy / GBC player)");
  printHardwareProfile(Serial, activeHardwareProfile());

  host.begin();
  Logger::info("gb", host.status());

  romStore.begin();
  Logger::info("romstore", romStore.status());

  ui.begin();
  video.begin();
  ui.drawPicker(romStore, selectedRom);

  eventLog.add("Cypher Boy booted");
  router.begin(Serial, "cypher-boy");
  router.on("status", "uptime, heap, profile, flags", cmdStatus);
  router.on("history", "recent events", cmdHistory);
  router.on("rom", "list ROMs found on SD", cmdRom);
  router.on("play", "launch a ROM by index: play 0", cmdPlay);
  router.on("screen", "picker | play [n]", cmdScreen);
  router.on("button", "inject a pad press: button up+a | button none", cmdButton);
  router.on("save", "force-write battery SRAM to SD now", cmdSave);
  router.on("touch", "print raw + mapped touch, taps, buttons, screen", cmdTouch);
  router.on("selftest", "drive the mock flow headlessly with PASS/FAIL", cmdSelfTest);
}

void loop() {
  router.poll();
  input.tick();

  if (screen == kScreenPlay) {
    if (input.menuPressed()) {
      toPicker();
    } else {
      const uint32_t pad = input.buttons();
      host.setPad(pad);
      host.runFrame(true);
      video.blit(host.framebuffer());
      ui.drawButtonState(pad);
      host.tickSave(millis());
    }
    // No delay: the emulator paces itself on the GB frame loop, and the
    // router/touch are polled once per frame.
    return;
  }

  // Picker: releasing on a ROM row launches it. Release-edge (not held) so a
  // drag that starts on one row and ends on another launches neither.
  if (input.releasedEdge()) {
    const int8_t row = GbUi::pickerHit(input.releaseX(), input.releaseY(), romStore.count());
    if (row >= 0) {
      selectedRom = row;
      if (!launchRom(row)) {
        // Still show the selection so the tap is not silently ignored.
        ui.drawPicker(romStore, selectedRom);
      }
    }
  }
  delay(5);
}
