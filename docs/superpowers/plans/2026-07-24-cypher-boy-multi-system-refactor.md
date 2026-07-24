# Cypher Boy — multi-system refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn Project 22 from a Game Boy player into a multi-system one, without destabilising the working Game Boy build — ending with Genesis playable and NES specced to drop in behind the same interface.

**Architecture:** Introduce an `EmuCore` interface that both callee-style cores (gnuboy, gwenesis) implement directly and the caller-style NES core can satisfy later via its own task. Generalise video, input layout, and the ROM store to be system-driven rather than `GB_*`-macro-driven. See [the design](../specs/2026-07-24-cypher-boy-multi-system-design.md).

**Tech Stack:** ESP32-P4 (esp32 core 3.3.8), arduino-cli, Arduino_GFX, `CrowPanelShared`, gnuboy + gwenesis (both GPLv2, C99).

**Conventions for every task:**
- FQBN: `esp32:esp32:esp32p4:USBMode=hwcdc,PSRAM=enabled,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600`
- Every task must end with **both** of these green:
  ```bash
  cd /Users/cypher/Documents/GitHub/crow-panel && arduino-cli compile \
    --fqbn "$FQBN" --libraries shared --build-path _arduino-build/p22-<task>-base \
    --build-property "tools.ctags.cmd.path=/usr/bin/true" projects/22-cypher-boy
  ```
  ```bash
  cd /Users/cypher/Documents/GitHub/crow-panel && arduino-cli compile \
    --fqbn "$FQBN" --libraries shared --build-path _arduino-build/p22-<task>-full \
    --build-property "tools.ctags.cmd.path=/usr/bin/true" \
    --build-property "compiler.cpp.extra_flags=-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1" \
    projects/22-cypher-boy
  ```
- **Verify library linkage, not just a green build**: `ls <build-path>/libraries/` must contain `SD_MMC` and `GFX_Library_for_Arduino` for the full build. A green compile alone once hid a completely dead SD path in project 15.
- `CTAGS_WORKAROUND=1` is mandatory (local ctags is broken). Because ctags is stubbed, **any function used before its definition in the `.ino` needs an explicit prototype** — there is a prototype block near the top; add to it.
- Keep every draw/touch path behind `#if USE_DISPLAY && defined(CONFIG_IDF_TARGET_ESP32P4)`, and keep hit-testers *outside* it so `selftest` can verify them headlessly.
- Do not commit — a concurrent session shares this working tree.

---

## Task 0: Centred handheld layout *(DONE — field-proven 2026-07-24)*

Screen centred with the pad derived from the viewport; `GbHitbox` gained a
`shape`; `GbUi` draws the table generically. **Confirmed on hardware** — the
derived layout needed no tuning, so `arm`, `r` and the `0.52f` vertical factor
stand as written.

- [x] `GB_VIEW_X` computed as `(1024 - GB_W * GB_SCALE) / 2`
- [x] `GbInput::buildLayout(vx, vy, vw, vh)` + `dpadBounds()`
- [x] `GbUi::drawPlayChrome` / `drawButtonState` iterate the layout by shape
- [x] Confirmed on hardware: pad comfortable, D-pad cross reads as one piece,
      no tuning required.

---

## Task 1: Extract `EmuCore` and make Game Boy implement it *(DONE)*

Pure refactor — no behaviour change. Prove the seam before adding a second core.

**Result:** `src/EmuCore.h` written; `GameBoyHost : public EmuCore` with all 21
methods overridden. `loadRom()` kept as a thin alias for `start()`. Flash cost of
the vtable + virtual dispatch: **+550 bytes** (417258 -> 417808 baseline), well
inside the 2 KB the step allowed. All six flag combos green. Selftest grew from
38 to 42 assertions — the four new ones exercise the seam polymorphically
(`system()`, `name()`, native size/scale, framebuffer identity) rather than just
proving it compiles. **Not yet run on hardware.**

Known follow-up: thumbnail dimensions are still `GameBoyHost::kThumbW/kThumbH`
statics that `GbUi` reads directly. That wants to become per-core when a second
console lands (Task 5), but generalising it now would be speculative.

**Files:**
- Create: `projects/22-cypher-boy/src/EmuCore.h`
- Modify: `src/GameBoyHost.h` / `.cpp` (implement the interface)
- Modify: `22-cypher-boy.ino` (talk to `EmuCore *` where practical)

- [x] **Step 1: Write the interface.**

```cpp
#ifndef CYPHER_BOY_EMU_CORE_H
#define CYPHER_BOY_EMU_CORE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class GbAudio;

enum EmuSystem : uint8_t { kSysGameBoy = 0, kSysGenesis, kSysNes, kSysCount };

// One emulated console. Lifecycle-shaped rather than strictly frame-shaped, so
// a core whose upstream loop blocks (nofrendo) can satisfy it from its own task
// while frame-driven cores (gnuboy, gwenesis) implement it directly.
class EmuCore {
 public:
  virtual ~EmuCore() {}
  virtual EmuSystem system() const = 0;
  virtual const char *name() const = 0;

  virtual bool begin(GbAudio *audio) = 0;
  virtual bool start(const String &romPath, const String &savePath) = 0;
  virtual void stop() = 0;

  virtual void runFrame(bool draw) = 0;
  virtual void setPad(uint32_t buttons) = 0;

  virtual const uint16_t *framebuffer() const = 0;
  virtual int16_t frameW() const = 0;
  virtual int16_t frameH() const = 0;
  virtual uint8_t scale() const = 0;  // integer upscale for this system

  virtual bool sramDirty() const = 0;
  virtual bool save() = 0;
  virtual void tickSave(uint32_t nowMs) = 0;
  virtual bool saveState(const String &p) = 0;
  virtual bool loadState(const String &p) = 0;
  virtual bool saveThumb(const String &statePath) = 0;

  virtual bool romLoaded() const = 0;
  virtual uint32_t frameCount() const = 0;
  virtual const String &status() const = 0;
};
#endif
```

- [x] **Step 2: Make `GameBoyHost` implement it.** Rename nothing yet; just add
      `: public EmuCore`, mark the matching methods `override`, and add the small
      new accessors:

```cpp
EmuSystem system() const override { return kSysGameBoy; }
const char *name() const override { return "Game Boy"; }
int16_t frameW() const override { return GB_W; }
int16_t frameH() const override { return GB_H; }
uint8_t scale() const override { return GB_SCALE; }
void stop() override { if (pendingSave_) save(); gnuboy_free_rom(); romLoaded_ = false; }
```
      `loadRom()` becomes `start()` (keep a thin `loadRom` alias so the `.ino`
      and selftest keep compiling in this step).

- [x] **Step 3: Compile both configs.** PASS; +550 bytes, within the ~2 KB of
      the previous build. A large jump means something was pulled in that should
      not have been.
- [x] **Step 4:** Build flashed and exercised on hardware 2026-07-24 with no
      behaviour change observed, which is what this pure refactor had to show.

---

## Task 2: Generalise video to any source size *(DONE)*

**Result:** `GbVideo::begin(srcW, srcH, scale)` replaces the `GB_*` macros with
runtime state; `centreX()` / `topY()` are pure and selftest-verified for all
three consoles. Re-allocates the strip on a geometry change, so switching cores
is safe. `GB_VIEW_X/Y` are no longer referenced outside `ProjectConfig.h`.
+568 bytes. The pad is now derived from `video.view*()` at boot, so screen and
controls move together by construction.

**Files:** Modify `src/GbVideo.h` / `.cpp`, `22-cypher-boy.ino`

- [ ] **Step 1: Replace the `GB_*` macros with runtime state.**

```cpp
class GbVideo {
 public:
  // Configure for a core's output. Allocates the scanline strip for `w*scale`.
  bool begin(int16_t srcW, int16_t srcH, uint8_t scale);
  void blit(const uint16_t *frame);
  void clearViewport();

  int16_t viewX() const { return vx_; }
  int16_t viewY() const { return vy_; }
  int16_t viewW() const { return srcW_ * scale_; }
  int16_t viewH() const { return srcH_ * scale_; }
  // Pure, for the selftest.
  static int16_t centreX(int16_t srcW, uint8_t scale) { return (1024 - srcW * scale) / 2; }
 private:
  uint16_t *strip_ = nullptr;
  int16_t srcW_ = 0, srcH_ = 0, vx_ = 0, vy_ = 0;
  uint8_t scale_ = 1;
  bool ready_ = false;
};
```

`begin()` computes `vx_ = centreX(srcW, scale)`, `vy_ = 84`, and allocates
`srcW*scale*scale` uint16 for the strip (Genesis at x2: 640*2 = 2560 px = 5 KB,
same order as today's 5.7 KB). Free and realloc on a system change.

- [ ] **Step 2: `blit()` loses the macros** — loop `srcH_` rows, expand each into
      the strip `scale_` times, `draw16bitRGBBitmap` per row, one flush at the end.
      Logic is unchanged; only the bounds become members.
- [ ] **Step 3: `.ino` calls `video.begin(core->frameW(), core->frameH(), core->scale())`**
      when a ROM is launched, and re-runs `GbInput::buildLayout(video.viewX(), video.viewY(), video.viewW(), video.viewH())` right after — the pad must follow the screen.
- [ ] **Step 4: Compile both configs.** PASS.
- [ ] **Step 5: Extend `selftest`** with the centring maths, which is pure:

```cpp
check("GB centres at 272", GbVideo::centreX(160, 3) == 272);
check("Genesis centres at 192", GbVideo::centreX(320, 2) == 192);
check("NES centres at 256", GbVideo::centreX(256, 2) == 256);
```

---

## Task 3: System-tagged ROM store *(DONE)*

**Result:** `GbRomStore::systemForName()` maps extensions to `EmuSystem`
(`.gb/.gbc` -> GB, `.md/.gen/.bin` -> Genesis, `.nes` -> NES, anything else
rejected); entries carry the tag and the picker badges each row. `USE_GENESIS_CORE`
/ `USE_NES_CORE` flags added (both 0) so `GbUi::systemPlayable()` can dim ROMs
this build has no core for, and `launchRom()` refuses them with a clear reason
rather than failing silently. +1266 bytes.

**Files:** Modify `src/GbRomStore.h` / `.cpp`, `src/GbUi.cpp` (row badge), `22-cypher-boy.ino`

- [ ] **Step 1: Tag entries by extension.**

```cpp
// in GbRomStore
EmuSystem systemOf(uint8_t index) const { return sys_[index]; }
static EmuSystem systemForName(const String &fileName);  // pure, selftest-able
// private: EmuSystem sys_[kMaxRoms];
```

```cpp
EmuSystem GbRomStore::systemForName(const String &fileName) {
  String l = fileName; l.toLowerCase();
  if (l.endsWith(".gb") || l.endsWith(".gbc")) return kSysGameBoy;
  if (l.endsWith(".md") || l.endsWith(".gen") || l.endsWith(".bin")) return kSysGenesis;
  if (l.endsWith(".nes")) return kSysNes;
  return kSysCount;  // unknown -> not listed
}
```
      `begin()` accepts a file when `systemForName() != kSysCount` and records the
      tag, replacing the current `.gb`/`.gbc`-only suffix check.

- [ ] **Step 2: Badge the picker rows.** `drawCartIcon` already tints `.gbc`
      differently; extend it to take an `EmuSystem` and tint per system, and draw
      a short system label ("GB" / "MD" / "NES") on the row.
- [ ] **Step 3: Refuse to launch a system with no core built in.** In `launchRom`,
      if `romStore.systemOf(i)` has no registered core, log
      `"[play] <name> needs USE_GENESIS_CORE=1"` and repaint the picker rather
      than failing silently.
- [ ] **Step 4: Compile both configs.** PASS.
- [ ] **Step 5: `selftest` additions:**

```cpp
check("tags .gb", GbRomStore::systemForName("x.GB") == kSysGameBoy);
check("tags .gbc", GbRomStore::systemForName("x.gbc") == kSysGameBoy);
check("tags .md", GbRomStore::systemForName("sonic.md") == kSysGenesis);
check("tags .nes", GbRomStore::systemForName("smb.nes") == kSysNes);
check("rejects junk", GbRomStore::systemForName("notes.txt") == kSysCount);
```

---

## Task 4: Vendor gwenesis and prove it builds

Riskiest unknown first — **flash cost** — before writing any integration.

**Files:** Create `projects/22-cypher-boy/src/gwenesis/**`, `src/gwenesis/VENDORED.md`

- [ ] **Step 1: Fetch the core** from `ducalex/retro-go`, path
      `gwenesis/components/gwenesis/src/` — subdirs `cpus/M68K`, `cpus/Z80`,
      `vdp`, `sound`, `bus`, plus top-level headers. Record the commit SHA.
      **Skip** `m68ki_instruction_jump_table_full.h` and `m68ki_cycles_full.h`
      (use the non-`_full` variants) and skip any `main/` app wrapper.

```bash
gh api "repos/ducalex/retro-go/git/trees/master?recursive=1" \
  --jq '.tree[] | select(.path|startswith("gwenesis/components/gwenesis/src/")) | .path'
```

- [ ] **Step 2: Add a `USE_GENESIS_CORE` flag, default 0**, in
      `config/ProjectConfig.h`, and guard the whole vendored tree's participation
      so the default build is untouched:

```cpp
#ifndef USE_GENESIS_CORE
#define USE_GENESIS_CORE 0
#endif
```
      Note: arduino-cli compiles every `.c` under `src/` regardless of flags, so
      each vendored `.c` needs `#if USE_GENESIS_CORE` … `#endif` wrapping its
      body, or the flash cost lands in every build. Do this as part of vendoring
      and note it in `VENDORED.md`.

- [ ] **Step 3: Measure.** Compile with `-DUSE_GENESIS_CORE=1` and record the
      flash delta versus without:

```bash
grep -o 'Sketch uses [0-9]*' /tmp/genesis-on.log
```
      Expected: under ~800 KB total. **If the app exceeds ~2.5 MB of the 3 MB
      partition, stop and report** — the remedy is trimming tables or a larger
      partition scheme, and that is a decision to raise, not to make silently.
- [ ] **Step 4: Compile both default configs.** PASS, and byte size **unchanged**
      from Task 3 — proving the vendored tree really is inert when the flag is off.

---

## Task 5: `GenesisCore` — silent, video + input only *(CODE DONE, UNPROVEN)*

**Result:** `GenesisCore : EmuCore` implemented with the scanline loop adapted
from retro-go's `main.c`. Compiles and links: **genesis 982 KB / 168 KB RAM**,
**genesis-full 1.19 MB / 176 KB RAM** — comfortably inside the 3 MB partition,
with 152 KB internal RAM left for stack and locals. All six Game Boy combos
unchanged.

Things the integration surfaced that the plan had not predicted:

- **gwenesis renders 8-bit palette indices, not RGB565.** The display applies
  `CRAM565` (byte-swapped) separately. Rather than convert into a second 143 KB
  buffer every frame, `EmuCore` gained optional `framebuffer8()` + `palette()`
  and `GbVideo::blitPaletted()` does the lookup while it is already expanding
  each scanline - so the palette costs nothing extra. The framebuffer is
  therefore 75 KB (PSRAM), not 143 KB.
- **gwenesis expects the application to define host globals** (`system_clock`,
  `scan_line`, the two audio buffers and their clocks/indices) plus a
  `gwenesis_io_get_buttons()` hook. The audio buffers must exist even in the
  silent build because `ym2612.c` and `gwenesis_sn76489.c` reference them at
  file scope.
- **Its pad enum is an index (0-7), not a bitmask, and orders the face buttons
  B, C, A.** Mapped explicitly in one table rather than by arithmetic.
- **`GbButton` moved to `EmuCore.h`** - it is the shared pad vocabulary, not a
  Game Boy detail.
- **`scripts/check-flag-matrix.sh` now passes flags to the C compiler too.**
  It only set `compiler.cpp.extra_flags`, which does not reach `.c` files, so a
  flag-gated C core would have compiled to nothing and still gone green.

**Not proven:** nothing has been run on hardware. No Genesis ROM has been
loaded, no frame rendered, no frame rate measured. Cartridge SRAM and save
states are stubbed for this core (they return false and say so).

**Files:** Create `src/GenesisCore.h` / `.cpp`; modify `22-cypher-boy.ino`

- [ ] **Step 1: Implement `EmuCore`** with the scanline loop adapted from
      retro-go's `gwenesis/main/main.c`:

```cpp
void GenesisCore::runFrame(bool draw) {
  if (!romLoaded_) return;
  scan_line = 0;
  const int lines = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
  while (scan_line < lines) {
    m68k_run(system_clock + VDP_CYCLES_PER_LINE);
    z80_run(system_clock + VDP_CYCLES_PER_LINE);
    // ym2612_run / SN76489 deliberately omitted until audio is its own step.
    if (draw && scan_line < screen_height) gwenesis_vdp_render_line(scan_line);
    system_clock += VDP_CYCLES_PER_LINE;
    scan_line++;
  }
  frames_++;
}
```
      Exact symbol names come from the vendored headers — **read them, do not
      assume**, the same discipline that caught the gnuboy palette and audio-enum
      mistakes.

- [ ] **Step 2: Framebuffer in PSRAM.** 320*224*2 = 143 KB via
      `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. Internal SRAM is already
      committed to the GB framebuffer, the video strip, and the audio scratch.
- [ ] **Step 3: Register the core** in the `.ino` behind `#if USE_GENESIS_CORE`,
      selected by `romStore.systemOf(index)`.
- [ ] **Step 4: Compile** default + `-DUSE_GENESIS_CORE=1 -DUSE_DISPLAY=1 -DUSE_GB_SD=1`. PASS.
- [ ] **Step 5: Hardware check (needs a Genesis ROM on the card).** Flash, launch
      it, and record honestly: does it boot, does it render, does touch drive it,
      **and what frame rate**. Add a `fps` counter to `status` if it is not
      obvious by eye. Slow-but-correct is a fine result to report; silent
      slowness is not.

---

## Task 6: Genesis audio (separate step, on purpose)

- [ ] **Step 1: Re-enable `ym2612_run()` and `gwenesis_SN76489_run()`** in the
      scanline loop, writing into the existing `GbAudio::submit()` path (it is
      already stereo S16 at `GB_SAMPLERATE`, which is what Genesis wants).
- [ ] **Step 2: Compile.** PASS.
- [ ] **Step 3: Hardware check.** Sound present, and **re-measure frame rate** —
      FM synthesis is the expensive part, and if it drops below playable, report
      that and gate it behind its own flag rather than shipping a slow default.

---

## Task 7: Docs + flag matrix

- [ ] **Step 1:** Add rows to `scripts/check-flag-matrix.sh` (re-read it first —
      a concurrent session edits it):
      `"$P22|genesis|-DUSE_GENESIS_CORE=1|"` and
      `"$P22|genesis-full|-DUSE_DISPLAY=1 -DUSE_GB_SD=1 -DUSE_GB_AUDIO=1 -DUSE_GENESIS_CORE=1|GFX Library for Arduino,SensorLib"`.
- [ ] **Step 2:** Update `README.md` (supported systems, SD layout, which
      extensions are recognised) and `TECHNICAL.md` (the `EmuCore` seam, the
      derived layout, the gwenesis vendoring + patches, measured flash/RAM/fps).
- [ ] **Step 3:** Record the proof state honestly — what was seen on hardware and
      what was not.
- [ ] **Step 4:** Compile the full matrix. All rows PASS.

---

## NES (Task 8+, after Genesis is proven)

Not planned in detail on purpose: the task-based wrapper for nofrendo's blocking
loop should be designed once `EmuCore` has been exercised by two real cores. The
constraint and the approach are captured in the design document.

## Self-review notes

- **Spec coverage:** layout (T0), `EmuCore` (T1), video (T2), ROM store (T3),
  vendoring/flash risk (T4), Genesis video+input (T5), Genesis audio (T6),
  docs/matrix (T7). NES deferred by design and documented.
- **Type consistency:** `EmuSystem` defined in `EmuCore.h`, consumed by
  `GbRomStore` and the picker; `frameW/frameH/scale` feed `GbVideo::begin()`
  which feeds `GbInput::buildLayout()`. Consistent.
- **Riskiest first:** flash cost (T4) is measured before any integration code is
  written, and Genesis comes up silent (T5) before audio (T6) — the same
  sequencing that made the Game Boy bring-up go cleanly.
