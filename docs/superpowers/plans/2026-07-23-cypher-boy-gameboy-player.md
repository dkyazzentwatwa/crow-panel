# Cypher Boy — Game Boy / GBC Player (Project 22) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone Arduino/`arduino-cli` project on the CrowPanel ESP32-P4 that plays Game Boy / GBC ROMs from SD with an on-screen touch gamepad and battery saves, using retro-go's `gnuboy` core.

**Architecture:** Vendor gnuboy's C core into `projects/22-cypher-boy/src/gnuboy/` and wrap it in a C++ host layer (`GameBoyHost`) that drives it through the shared CrowPanel infra — `CrowDisplay` canvas + internal-SRAM offscreen blit for video, `CrowTouch` for the gamepad, `SD_MMC` (FAT VFS) so gnuboy's stdio ROM/save file calls work at `/sdcard/...`. The `.ino` stays thin; every touch action has a serial twin. Video-only in v1 (silent).

**Tech Stack:** ESP32-P4 (esp32 core 3.3.8), arduino-cli, Arduino_GFX, shared `CrowPanelShared` lib, gnuboy (GPLv2, C99).

**Conventions for every task below:**
- FQBN: `esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600`
- Baseline compile:
  ```bash
  cd /Users/cypher/Documents/GitHub/crow-panel && arduino-cli compile \
    --fqbn "$FQBN" --libraries shared \
    --build-path _arduino-build/22-cypher-boy-baseline \
    --build-property "tools.ctags.cmd.path=/usr/bin/true" \
    projects/22-cypher-boy
  ```
- Display compile: append `--build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1"` and use build-path `…-display`.
- All draw/touch code lives behind `#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)`; headless must stay green.
- Commits are **scoped to this project only**: `git add projects/22-cypher-boy` (never `git add -A` — a concurrent session is editing other files on `main`). Confirm branch/commit policy with the user before the first commit.
- "Test" in this embedded context = (a) the compile gate and (b) `selftest` serial assertions on real state. There is no host pytest harness; follow the suite's existing compile+selftest proof model (see `projects/10-litego-touch-coach`).

---

## Task 0: Scaffold + vendor gnuboy, compile the bare core headless

Riskiest unknown first: does gnuboy's C compile and link under arduino-cli for the P4? Prove it before writing any glue.

**Files:**
- Create: `projects/22-cypher-boy/22-cypher-boy.ino`
- Create: `projects/22-cypher-boy/config/ProjectConfig.h`
- Create: `projects/22-cypher-boy/src/gnuboy/{cpu,gnuboy,hw,lcd,sound}.c` + `{cpu,gnuboy,hw,lcd,sound}.h` + `tables.h` + `COPYING` + `CREDITS` (vendored verbatim from retro-go `retro-core/components/gnuboy/`)
- Create: `projects/22-cypher-boy/src/gnuboy/VENDORED.md` (source commit + list of local patches)

- [ ] **Step 1: Vendor the core.** Download the 8 source files + `COPYING`/`CREDITS` from `ducalex/retro-go` path `retro-core/components/gnuboy/` at current `master`. Record the commit SHA in `VENDORED.md`. Do NOT copy `CMakeLists.txt` (arduino-cli discovers `.c` automatically).
  ```bash
  BASE="repos/ducalex/retro-go/contents/retro-core/components/gnuboy"
  DST="projects/22-cypher-boy/src/gnuboy"
  mkdir -p "$DST"
  for f in cpu.c cpu.h gnuboy.c gnuboy.h hw.c hw.h lcd.c lcd.h sound.c sound.h tables.h COPYING CREDITS; do
    gh api "$BASE/$f" --jq '.content' | base64 -d > "$DST/$f"
  done
  gh api "repos/ducalex/retro-go/commits/master" --jq '.sha' # -> record in VENDORED.md
  ```

- [ ] **Step 2: PSRAM allocation patch.** In `src/gnuboy/gnuboy.c`, the ROM/RAM bank allocations must use PSRAM (1–2 MB won't fit internal RAM). Add near the top of `gnuboy.c` after the includes:
  ```c
  #include "esp_heap_caps.h"
  #define GB_PSRAM_MALLOC(sz)      heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define GB_PSRAM_CALLOC(n, sz)   heap_caps_calloc((n), (sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  ```
  Then replace the bank allocations (the `cart.rombanks[bank] = malloc(BANK_SIZE)`, `cart.rambanks = calloc(cart.ramsize, 0x2000)`, and `cart.rombanks = calloc(cart.romsize, sizeof(byte *))` sites) with `GB_PSRAM_MALLOC` / `GB_PSRAM_CALLOC`. Leave the small `hw.bios = malloc(0x900)` as-is. Record each edited line in `VENDORED.md`.

- [ ] **Step 3: Minimal `.ino` that references the core.** This forces the linker to pull in gnuboy so a link error surfaces now, not later.
  ```cpp
  #include "config/ProjectConfig.h"
  #include <CrowPanelShared.h>
  extern "C" {
  #include "src/gnuboy/gnuboy.h"
  }
  SerialCommandRouter router;

  void cmdStatus(const String &) { printSystemStatus(Serial, "cypher-boy", 0); }

  void setup() {
    Logger::begin(115200);
    Logger::info("app", "CrowPanel Cypher Boy (Game Boy player)");
    printHardwareProfile(Serial, activeHardwareProfile());
    // Reference a gnuboy symbol so it links; real init comes in Task 2.
    Logger::info("gb", String("gnuboy hwtype slot=") + gnuboy_get_hwtype());
    router.begin(Serial, "cypher-boy");
    router.on("status", "uptime, heap, profile", cmdStatus);
  }
  void loop() { router.poll(); delay(5); }
  ```

- [ ] **Step 4: `config/ProjectConfig.h`** with flag defaults + touch calibration + layout constants:
  ```cpp
  #ifndef CYPHER_BOY_PROJECT_CONFIG_H
  #define CYPHER_BOY_PROJECT_CONFIG_H
  #ifndef USE_DISPLAY
  #define USE_DISPLAY 0
  #endif
  #ifndef USE_GB_SD          // SD ROM/save storage; mock/no-SD path when 0
  #define USE_GB_SD 0
  #endif
  // GB frame + viewport (x3 integer scale, centered-left; controls on the right/bottom)
  #define GB_W 160
  #define GB_H 144
  #define GB_SCALE 3
  #define GB_VIEW_X 40
  #define GB_VIEW_Y 60
  // SD layout (FAT VFS mount point from SD_MMC)
  #define GB_SD_ROOT   "/sdcard"
  #define GB_ROM_DIR   "/sdcard/roms"
  #define GB_SAVE_DIR  "/sdcard/saves"
  #endif
  ```

- [ ] **Step 5: Compile headless.** Run the baseline compile command. Expected: **SUCCESS**. If unresolved symbols appear (e.g. a `MESSAGE_*` macro or missing include), resolve by adding the missing standard include to the vendored file and note it in `VENDORED.md`; do not stub gnuboy logic. Re-run until green.

- [ ] **Step 6: Compile display.** Run the display compile command. Expected: **SUCCESS** (no draw code yet, just proves the flag path builds).

- [ ] **Step 7: Commit.**
  ```bash
  git add projects/22-cypher-boy
  git commit -m "feat(22-cypher-boy): scaffold + vendor gnuboy core, compiles on P4"
  ```

---

## Task 1: SD mount + ROM store (list ROMs, resolve save paths)

**Files:**
- Create: `projects/22-cypher-boy/src/GbRomStore.h`
- Create: `projects/22-cypher-boy/src/GbRomStore.cpp`
- Modify: `projects/22-cypher-boy/22-cypher-boy.ino` (mount SD in setup, add `rom` command)

- [ ] **Step 1: Interface.** `GbRomStore` owns SD and the `/roms` + `/saves` layout. Headless (`USE_GB_SD=0`) it returns a built-in mock list so the UI and selftest work with no card.
  ```cpp
  #ifndef CYPHER_BOY_ROM_STORE_H
  #define CYPHER_BOY_ROM_STORE_H
  #include "../config/ProjectConfig.h"
  #include <Arduino.h>
  class GbRomStore {
   public:
    static const uint8_t kMaxRoms = 32;
    bool begin();                         // mount SD (USE_GB_SD) or seed mock list
    uint8_t count() const { return count_; }
    const String &name(uint8_t i) const { return names_[i < count_ ? i : 0]; }
    String romPath(uint8_t i) const;      // GB_ROM_DIR "/" name  (or mock)
    String savePath(uint8_t i) const;     // GB_SAVE_DIR "/" base ".sav"
    bool ready() const { return ready_; }
   private:
    String names_[kMaxRoms];
    uint8_t count_ = 0;
    bool ready_ = false;
  };
  #endif
  ```

- [ ] **Step 2: Implementation.** `begin()` under `USE_GB_SD`: `SD_MMC.begin()` (the CrowPanel SD_MMC bring-up — reuse the pin setup from `projects/18-cypher-desk-panel` / `projects/15-pokedex-panel`, whichever mounts SD_MMC; match it exactly), then `mkdir` `/roms` `/saves` if missing, then iterate `/roms` collecting `*.gb` / `*.gbc` into `names_`. Headless: seed `names_[0]="pokemon-demo.gb"`, `count_=1`, `ready_=true`. `savePath` swaps the extension for `.sav` under `GB_SAVE_DIR`. Full listing code follows the `File dir = SD_MMC.open(GB_ROM_DIR); for (File f = dir.openNextFile(); f; f = dir.openNextFile())` pattern; filter by suffix `.gb`/`.gbc` case-insensitively; cap at `kMaxRoms`.

- [ ] **Step 3: Wire into `.ino`.** In `setup()` after `router.begin`, call `romStore.begin()` and log `count()`. Add:
  ```cpp
  void cmdRom(const String &) {
    Serial.printf("[rom] %u ROM(s), SD %s\n", romStore.count(), romStore.ready() ? "ready" : "mock");
    for (uint8_t i = 0; i < romStore.count(); i++)
      Serial.printf("  %u: %s\n", i, romStore.name(i).c_str());
  }
  // router.on("rom", "list ROMs found on SD", cmdRom);
  ```

- [ ] **Step 4: Compile baseline + display.** Both green.

- [ ] **Step 5: Commit.** `git add projects/22-cypher-boy && git commit -m "feat(22-cypher-boy): SD ROM store + rom command"`

---

## Task 2: GameBoyHost — load a ROM and run frames headless

**Files:**
- Create: `projects/22-cypher-boy/src/GameBoyHost.h`
- Create: `projects/22-cypher-boy/src/GameBoyHost.cpp`
- Modify: `.ino` (construct host, add `selftest` first assertions)

- [ ] **Step 1: Interface.** The only TU that talks to gnuboy.
  ```cpp
  #ifndef CYPHER_BOY_HOST_H
  #define CYPHER_BOY_HOST_H
  #include "../config/ProjectConfig.h"
  #include <Arduino.h>
  #include "GbRomStore.h"
  class GameBoyHost {
   public:
    bool begin();                          // gnuboy_init(RGB565, video_cb), alloc framebuffer
    bool loadRom(const String &romPath, const String &savePath);  // load rom+sram
    void runFrame(bool draw);              // gnuboy_run(draw)
    void setPad(uint32_t buttons);         // gnuboy_set_pad bitfield
    bool sramDirty();                      // gnuboy_sram_dirty
    void save();                           // gnuboy_save_sram(savePath_, false)
    const uint16_t *framebuffer() const { return fb_; }   // 160x144 RGB565
    bool romLoaded() const { return romLoaded_; }
    uint32_t frameCount() const { return frames_; }
   private:
    uint16_t *fb_ = nullptr;               // GB_W*GB_H, internal SRAM
    String savePath_;
    bool romLoaded_ = false;
    uint32_t frames_ = 0;
  };
  // gnuboy button bits (from gnuboy.h PAD_*): expose our own stable names
  enum GbButton : uint32_t {
    GB_RIGHT=0x01, GB_LEFT=0x02, GB_UP=0x04, GB_DOWN=0x08,
    GB_A=0x10, GB_B=0x20, GB_SELECT=0x40, GB_START=0x80
  };
  #endif
  ```
  Confirm the bit values against `gnuboy.h`'s `PAD_*` / `gb_pad_t` enum during implementation and map ours to gnuboy's in `setPad` (do not assume — read the header).

- [ ] **Step 2: Implementation.**
  ```cpp
  #include "GameBoyHost.h"
  extern "C" {
  #include "gnuboy/gnuboy.h"
  }
  static GameBoyHost *s_host = nullptr;
  static void gb_video_cb(void *buffer) { /* frame ready in fb_; blit happens in GbVideo (Task 3) */ }

  bool GameBoyHost::begin() {
    fb_ = (uint16_t *)heap_caps_malloc(GB_W * GB_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!fb_) { Logger::error("gb", "framebuffer alloc failed"); return false; }
    s_host = this;
    // GB_PIXEL_565_LE / GB_AUDIO_* names per gnuboy.h; audio disabled (v1 silent).
    if (gnuboy_init(0, GB_AUDIO_NONE, GB_PIXEL_565_LE, gb_video_cb, nullptr) != 0) return false;
    gnuboy_set_framebuffer(fb_);
    return true;
  }
  bool GameBoyHost::loadRom(const String &romPath, const String &savePath) {
    if (gnuboy_load_rom_file(romPath.c_str()) != 0) { Logger::error("gb","rom load failed"); return false; }
    savePath_ = savePath;
    gnuboy_load_sram(savePath_.c_str());   // ok if file absent
    gnuboy_reset(true);
    romLoaded_ = true; frames_ = 0;
    return true;
  }
  void GameBoyHost::runFrame(bool draw) { if (romLoaded_) { gnuboy_run(draw); frames_++; } }
  void GameBoyHost::setPad(uint32_t b) { gnuboy_set_pad((int)b); }   // map to PAD_* in impl
  bool GameBoyHost::sramDirty() { return gnuboy_sram_dirty(); }
  void GameBoyHost::save() { if (romLoaded_) gnuboy_save_sram(savePath_.c_str(), false); }
  ```
  **CORRECTED during implementation** (the header was read, as this step required):
  - There is **no `GB_AUDIO_NONE`** — only `GB_AUDIO_STEREO_S16` / `GB_AUDIO_MONO_S16`. Silence
    comes from a **NULL audio callback**, not an audio format. Do NOT pass `samplerate = 0`:
    `gb_sound_reset()` computes `(1<<21) / (double)samplerate` and casting the resulting
    infinity to `int` is undefined behaviour. Use `GB_SAMPLERATE` (32000) + a small throwaway
    sound buffer + NULL callback.
  - **No video callback is needed.** `gnuboy_run(draw)` renders scanlines directly into the
    framebuffer; the callback is only a notification, and the "draw inside the callback"
    caveat in gnuboy.c applies solely to `GB_PIXEL_PALETTED`. We use `GB_PIXEL_565_LE` and
    blit from the loop, so both callbacks are `nullptr`.
  - Pad bits are `GB_PAD_*` (RIGHT 0x01 … START 0x80); `GameBoyHost.cpp` `static_assert`s our
    `GbButton` values against them so a future gnuboy bump cannot silently rewire the gamepad.

- [ ] **Step 3: `selftest` scaffold in `.ino`** (grows across later tasks):
  ```cpp
  void cmdSelfTest(const String &) {
    int pass=0, fail=0;
    auto check=[&](const char*n,bool ok){ Serial.printf("[selftest] %-28s %s\n",n,ok?"PASS":"FAIL"); ok?pass++:fail++; };
    check("host begin", host.begin());
  #if !USE_GB_SD
    // headless: load the bundled test ROM path; on a real card use romStore
  #endif
    bool loaded = host.loadRom(romStore.romPath(0), romStore.savePath(0));
    check("rom loads", loaded);
    uint32_t f0 = host.frameCount();
    for (int i=0;i<10;i++) host.runFrame(false);
    check("frames advance", host.frameCount() == f0 + 10);
    Serial.printf("[selftest] %d passed, %d failed\n", pass, fail);
  }
  // router.on("selftest","drive the mock flow headlessly with PASS/FAIL", cmdSelfTest);
  ```
  Note: headless `loadRom` needs a real ROM file reachable via stdio. For headless selftest without a card, guard the rom-load asserts behind `USE_GB_SD` and, when `USE_GB_SD=0`, assert only `host.begin()` + that `runFrame` is a safe no-op when no ROM is loaded. Keep both branches honest (no fake PASS).

- [ ] **Step 4: Compile baseline + display.** Both green. (Selftest ROM-load path only exercised on-device with a card.)

- [ ] **Step 5: Commit.** `git add projects/22-cypher-boy && git commit -m "feat(22-cypher-boy): GameBoyHost wraps gnuboy (load+run+save)"`

---

## Task 3: GbVideo — offscreen canvas + ×3 blit (display build)

**Files:**
- Create: `projects/22-cypher-boy/src/GbVideo.h` / `.cpp`
- Modify: `GameBoyHost` video callback to call GbVideo blit; `.ino` loop to render.

- [ ] **Step 1: Interface.**
  ```cpp
  #ifndef CYPHER_BOY_VIDEO_H
  #define CYPHER_BOY_VIDEO_H
  #include "../config/ProjectConfig.h"
  #include <Arduino.h>
  class GbVideo {
   public:
    bool begin();                                  // no-op headless; alloc canvas on display
    void blit(const uint16_t *fb160x144);          // scale ×3 into viewport + flush region
    // exposed for selftest: where a GB pixel lands on-panel
    static int viewX(int gx) { return GB_VIEW_X + gx * GB_SCALE; }
    static int viewY(int gy) { return GB_VIEW_Y + gy * GB_SCALE; }
   private:
  #if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
    class Arduino_Canvas *canvas_ = nullptr;       // 160x144 internal SRAM
    bool ready_ = false;
  #endif
  };
  #endif
  ```

- [ ] **Step 2: Implementation.** `begin()` (display+P4): create a `GB_W×GB_H` `Arduino_Canvas` backed by internal SRAM (mirror `ensurePongCanvas()` in `projects/08-cypher-gamer-arcade/src/ArcadeEngine.cpp` — same `PongCanvas`/`alloc()`/`internal()` idiom). `blit()`: draw `fb160x144` into the canvas, then nearest-neighbor ×3 into the panel via `CrowDisplay::canvas()` — write scaled rows and `CrowDisplay::flush(GB_VIEW_X, GB_VIEW_Y, GB_W*GB_SCALE, GB_H*GB_SCALE)` for the region only. Headless: `begin()`/`blit()` are no-ops. Keep the whole body behind the `USE_DISPLAY && P4` guard; provide empty stubs otherwise.

- [ ] **Step 3: Hook the callback.** In `GameBoyHost`, give the static `gb_video_cb` access to a `GbVideo*` (set in `begin`) and call `video_->blit(fb_)` there, OR simpler: have the `.ino` loop call `video.blit(host.framebuffer())` after `host.runFrame(true)`. Choose the loop-driven approach (simpler, no static coupling):
  ```cpp
  void loop() {
    router.poll();
    if (host.romLoaded()) { host.runFrame(true); video.blit(host.framebuffer()); }
    delay(1);
  }
  ```

- [ ] **Step 4: selftest add** — scale math is pure, assert it headless:
  ```cpp
  check("viewport scale x", GbVideo::viewX(159) == GB_VIEW_X + 159*GB_SCALE);
  check("viewport scale y", GbVideo::viewY(143) == GB_VIEW_Y + 143*GB_SCALE);
  ```

- [ ] **Step 5: Compile baseline + display.** Both green.

- [ ] **Step 6: Commit.** `git add projects/22-cypher-boy && git commit -m "feat(22-cypher-boy): GbVideo offscreen canvas + x3 blit"`

---

## Task 4: GbInput — touch → pad bitfield

**Files:** Create `src/GbInput.h` / `.cpp`; modify `.ino` loop to feed pad + add `button` command.

- [ ] **Step 1: Layout + interface.** Fixed on-screen control rectangles (right column + bottom for the ×3 viewport that ends at x=520). D-pad left cluster, A/B right cluster, Start/Select center-bottom, MENU corner.
  ```cpp
  #ifndef CYPHER_BOY_INPUT_H
  #define CYPHER_BOY_INPUT_H
  #include "../config/ProjectConfig.h"
  #include "GameBoyHost.h"   // GbButton
  #include <Arduino.h>
  struct GbHitbox { int16_t x,y,w,h; uint32_t bit; const char *label; };
  class GbInput {
   public:
    void begin();
    uint32_t poll();               // returns held GbButton bitfield this frame
    bool menuPressed();            // edge: MENU tapped
    static const GbHitbox *layout(uint8_t &n);  // for GbUi to draw + selftest
    uint32_t mapPoint(int16_t x, int16_t y) const;  // pure: point -> bits (testable)
   private:
    CrowTouch touch_;
    bool menuEdge_ = false;
  };
  #endif
  ```
  Define the hitbox table in the `.cpp` with concrete coordinates (viewport occupies x≈40..520; controls live x≈560..1000): D-pad UP `{620,360,80,80,GB_UP}`, DOWN `{620,520,80,80,GB_DOWN}`, LEFT `{540,440,80,80,GB_LEFT}`, RIGHT `{700,440,80,80,GB_RIGHT}`, B `{820,470,90,90,GB_B}`, A `{930,430,90,90,GB_A}`, SELECT `{560,300,120,44,GB_SELECT}`, START `{700,300,120,44,GB_START}`, MENU `{940,20,64,44, 0 /*special*/}`. Tune on glass later.

- [ ] **Step 2: Implementation.** `poll()`: `touch_.tick()`; if `touch_.down()`, OR together every hitbox `bit` whose rect contains `(touch_.x(),touch_.y())` via `Widgets::hitRect` — multiple simultaneous presses allowed (e.g. UP+A). MENU is edge-triggered on `releasedEdge()` inside its rect → set `menuEdge_`. `mapPoint` is the pure core (no touch object) used by both `poll` and selftest. Headless: `touch_` is the never-pressed stub, so `poll` returns 0 — fine.

- [ ] **Step 3: Wire loop + serial parity.**
  ```cpp
  void loop() {
    router.poll();
    if (host.romLoaded()) {
      uint32_t pad = input.poll();
      host.setPad(pad);
      host.runFrame(true);
      video.blit(host.framebuffer());
      if (input.menuPressed()) toMenu();   // defined in Task 5
    }
    delay(1);
  }
  void cmdButton(const String &a){ /* inject: "button a", "button up+a", "button none" for headless test */ }
  // router.on("button","inject a gamepad press (serial parity for touch)", cmdButton);
  ```

- [ ] **Step 4: selftest add** — pure mapping asserts, center of each hitbox → its bit:
  ```cpp
  uint8_t n; const GbHitbox *L = GbInput::layout(n);
  bool mapOk = true;
  for (uint8_t i=0;i<n;i++){ if(!L[i].bit) continue;
    uint32_t b = input.mapPoint(L[i].x+L[i].w/2, L[i].y+L[i].h/2);
    mapOk = mapOk && (b & L[i].bit); }
  check("touch maps to buttons", mapOk);
  check("empty space -> no button", input.mapPoint(10,10) == 0);
  ```

- [ ] **Step 5: Compile baseline + display.** Both green.
- [ ] **Step 6: Commit.** `git add projects/22-cypher-boy && git commit -m "feat(22-cypher-boy): GbInput touch gamepad + button command"`

---

## Task 5: GbUi — ROM picker + in-game control overlay

**Files:** Create `src/GbUi.h` / `.cpp`; modify `.ino` (screen state, `toMenu()`, `screen` command).

- [ ] **Step 1: Screen model + interface.** Two screens: `kPicker` (list ROMs, tap to launch) and `kPlay` (gamepad overlay around the live viewport). The `.ino` owns the `Screen` enum + transitions; `GbUi` draws.
  ```cpp
  #ifndef CYPHER_BOY_UI_H
  #define CYPHER_BOY_UI_H
  #include "../config/ProjectConfig.h"
  #include "GbRomStore.h"
  #include <Arduino.h>
  class GbUi {
   public:
    bool begin();                          // CrowDisplay::begin(..., /*manualFlush=*/true)
    void drawPicker(const GbRomStore &roms, int8_t selected);
    int8_t pickerHit(int16_t x, int16_t y, const GbRomStore &roms); // row -> index or -1
    void drawPlayChrome();                 // static gamepad overlay (once per entry to kPlay)
   private:
  #if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)
    bool ready_ = false;
  #endif
  };
  #endif
  ```

- [ ] **Step 2: Implementation.** `begin()` → `CrowDisplay::begin(activeHardwareProfile(), "Cypher Boy", true)`. `drawPicker`: `Widgets::headerBar(g,"Cypher Boy","Game Boy / GBC")` + a vertical list of ROM names as `Widgets::panel` rows (reuse the row/list idiom from `projects/15-pokedex-panel/src/PokedexDashboard.cpp`), highlight `selected`. `pickerHit`: map y to a row index, bounds-check against `roms.count()`. `drawPlayChrome`: fill background, draw the `GbInput` hitboxes as labeled `Widgets::touchButton`s (iterate `GbInput::layout`), leaving the viewport rect clear for `GbVideo`. All behind the display guard; headless stubs return safe defaults (`pickerHit` still computes row math so selftest can check it).

- [ ] **Step 3: `.ino` screen glue.**
  ```cpp
  enum Screen { kPicker=0, kPlay };
  Screen screen = kPicker; int8_t sel = 0;
  void toMenu(){ if (host.sramDirty()) host.save(); screen=kPicker; ui.drawPicker(romStore, sel); }
  void launch(int8_t i){ if(host.loadRom(romStore.romPath(i),romStore.savePath(i))){ screen=kPlay; ui.drawPlayChrome(); } }
  ```
  In `loop()`, branch on `screen`: `kPicker` reads a touch release, `int8_t r=ui.pickerHit(x,y,romStore); if(r>=0){ sel=r; launch(r);} `; `kPlay` runs the emulator block from Task 4. Add `router.on("screen","picker|play, or 'screen play <n>' to launch", cmdScreen)` mirroring both transitions.

- [ ] **Step 4: selftest add** — picker row math is pure:
  ```cpp
  check("picker hit row 0", ui.pickerHit(/*x*/200, /*first-row y*/ 120, romStore) == 0);
  check("picker miss header", ui.pickerHit(200, 20, romStore) < 0);
  ```

- [ ] **Step 5: Compile baseline + display.** Both green.
- [ ] **Step 6: Commit.** `git add projects/22-cypher-boy && git commit -m "feat(22-cypher-boy): ROM picker + in-game gamepad overlay"`

---

## Task 6: Battery saves — dirty cadence + flush/reload

**Files:** Modify `GameBoyHost` (debounced autosave), `.ino` loop.

- [ ] **Step 1: Debounced autosave in host.** Add to `GameBoyHost`:
  ```cpp
  void tickSave(uint32_t nowMs);   // call each frame; flush if dirty & idle >2s
  // private: uint32_t lastDirtyMs_=0; bool pendingSave_=false;
  ```
  Impl: after `runFrame`, if `gnuboy_sram_dirty()` set `pendingSave_=true,lastDirtyMs_=now`; in `tickSave`, if `pendingSave_ && now-lastDirtyMs_>2000` → `gnuboy_save_sram(savePath_,false); pendingSave_=false;`. Also force `save()` on `toMenu()` (already wired) and expose a `save` serial command.

- [ ] **Step 2: Loop + command.** Call `host.tickSave(millis())` in the `kPlay` branch. Add `router.on("save","force-write battery SRAM to SD now", cmdSave)`.

- [ ] **Step 3: selftest add — save round-trip** (only meaningful with a card; guard under `USE_GB_SD`, else assert the dirty-flag state machine with a fake clock):
  ```cpp
  #if USE_GB_SD
  host.save(); check("sram file written", SD_MMC.exists(romStore.savePath(0)));
  #endif
  ```

- [ ] **Step 4: Compile baseline + display.** Both green.
- [ ] **Step 5: Commit.** `git add projects/22-cypher-boy && git commit -m "feat(22-cypher-boy): debounced battery saves to SD"`

---

## Task 7: Flag matrix registration + docs + final selftest

**Files:** Create `README.md`, `TECHNICAL.md`; modify (SHARED — coordinate with concurrent session, re-read before editing) `scripts/project-registry.sh`, `scripts/check-flag-matrix.sh`.

- [ ] **Step 1: README.md** — user-facing: what it is (touch Game Boy/GBC player), SD layout (`/roms`, `/saves`), how to add a ROM, the touch controls, honest status (compile-ready; on-panel play + your own legally-obtained ROM required; silent in v1). MIT-vs-GPL note: this folder is GPLv2 (gnuboy).

- [ ] **Step 2: TECHNICAL.md** — match the `projects/19`/`21` section shape: AI setup prompt, Screens, Touch controls, Serial commands, Feature flags (`USE_DISPLAY`, `USE_GB_SD`), gnuboy vendoring + local patches (point at `src/gnuboy/VENDORED.md`), SD/PSRAM notes, Build, Proof state (compile-ready; enumerate the on-panel acceptance steps: ROM boots, touch drives, save survives reboot, GBC color).

- [ ] **Step 3: Register (shared files — `git pull`/re-read first).** Add `projects/22-cypher-boy` to `scripts/project-registry.sh`. Add rows to `scripts/check-flag-matrix.sh`: `P22=projects/22-cypher-boy`; `"$P22|baseline||"`, `"$P22|display|-DUSE_DISPLAY=1|GFX Library for Arduino,SensorLib"`, `"$P22|gb-sd|-DUSE_GB_SD=1|"`, `"$P22|display-gb-sd|-DUSE_DISPLAY=1 -DUSE_GB_SD=1|GFX Library for Arduino,SensorLib"`.

- [ ] **Step 4: Full selftest + flag combos.** Run `selftest` output review (all PASS in the headless branch). Compile all four flag combos:
  ```bash
  for tag in "" "-DUSE_DISPLAY=1" "-DUSE_GB_SD=1" "-DUSE_DISPLAY=1 -DUSE_GB_SD=1"; do
    arduino-cli compile --fqbn "$FQBN" --libraries shared \
      --build-path "_arduino-build/22-verify-${tag// /_}" \
      --build-property "tools.ctags.cmd.path=/usr/bin/true" \
      ${tag:+--build-property "compiler.cpp.extra_flags=$tag"} projects/22-cypher-boy || echo "FAIL: $tag"
  done
  ```
  Expected: 4× success, no FAIL.

- [ ] **Step 5: Commit.**
  ```bash
  git add projects/22-cypher-boy scripts/project-registry.sh scripts/check-flag-matrix.sh
  git commit -m "feat(22-cypher-boy): docs + flag-matrix registration"
  ```

---

## Proof boundary (honest close)

Everything above lands at **compile-ready** (4 flag combos green) + `selftest` PASS in the headless branch. **Not** done here, because no panel/ROM is attached this session: a Game Boy ROM actually booting, touch driving gameplay, a battery save surviving a reboot, and GBC color output. Those are the on-panel acceptance steps for the user, with a legally-obtained ROM they supply (a free homebrew `.gb` is the recommended demo asset — no ROM is committed to the repo). Audio is the first follow-up after this slice is proven on glass.

## Self-review notes

- **Spec coverage:** vendoring+patch (T0), SD/ROM store (T1), host/run (T2), video+scale (T3), touch gamepad (T4), picker+overlay (T5), saves (T6), flags+docs (T7) — every spec section maps to a task.
- **Type consistency:** `GbButton` bits defined in `GameBoyHost.h`, consumed by `GbInput`; `GbHitbox`/`layout()` shared by `GbInput`+`GbUi`; `romPath`/`savePath` defined in `GbRomStore`, consumed by `GameBoyHost`/`.ino`. Consistent.
- **Known unknowns to resolve while implementing (read the header/source, don't guess):** exact `gnuboy.h` enum names (`GB_PIXEL_565_LE`, `GB_AUDIO_NONE`, `PAD_*`) and the precise gnuboy allocation call-sites for the PSRAM patch. Both are inspected, not invented, in T0/T2.
