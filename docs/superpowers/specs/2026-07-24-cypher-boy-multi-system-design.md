# Cypher Boy — multi-system support (Genesis, then NES) — Design

Date: 2026-07-24 (supersedes the 2026-07-23 NES-only notes)
Status: approved for planning

## Context

Project 22 plays Game Boy / GBC on the CrowPanel, field-proven with video, touch,
SD, sound, save states and themes. The goal now is more consoles. The original
notes assumed NES was the natural second system; **investigating the actual
retro-go cores reversed that**, and this document records why.

## The finding that sets the order: control flow, not console size

The two candidate cores drive the program in opposite directions.

**Gwenesis (Genesis / Mega Drive) is callee-style** — the host owns the loop,
exactly like gnuboy. From retro-go's `gwenesis/main/main.c`:

```c
while (scan_line < lines_per_frame) {
    m68k_run(system_clock + VDP_CYCLES_PER_LINE);
    z80_run(system_clock + VDP_CYCLES_PER_LINE);
    ym2612_run(system_clock + VDP_CYCLES_PER_LINE);
    gwenesis_vdp_render_line(scan_line);
}
```

That is the same shape as `GameBoyHost::runFrame()` calling `gnuboy_run()`. It
drops into the existing architecture almost unchanged.

**Nofrendo (NES) is caller-style** — `nofrendo_start(filename, savefile)` blocks
until the game stops and calls *your* `blit` / `vsync` / `input` callbacks. It
wants to own the program. Wrapping it means a dedicated FreeRTOS task and
cross-task frame marshalling.

So the "bigger" console is the simpler integration:

| | Gwenesis (Genesis) | Nofrendo (NES) |
|---|---|---|
| Control flow | callee — we own the loop | **caller — blocks, needs its own task** |
| Source files | ~37 `.c/.h` | ~85 |
| Runtime cost | high (68000 + Z80 + VDP + YM2612 FM) | low (6502 is trivial for a P4) |
| Big data | M68K jump/cycle tables, ~300-600 KB flash | `database.h`, 401 KB |

**Order: shared refactor → Genesis → NES.** Doing Genesis second means the
multi-system plumbing is designed against two cores that share a control-flow
model; NES's task wrapper then lands last as an isolated special case rather
than as the thing that defines the abstraction.

## Layout: screen centred, controls either side

The old layout (screen left, controls crammed right) is replaced by a handheld
arrangement: **screen centred, D-pad in the left margin, A/B in the right,
START/SELECT under the screen, MENU top-left**.

This is not cosmetic — it is what makes multi-system viable. The pad must be
**derived from the viewport rect**, not hardcoded, because each console has a
different native resolution and therefore different side margins:

| System | Native | Scale | Screen | Side margins | START/SELECT |
|---|---|---|---|---|---|
| Game Boy | 160x144 | x3 | 480x432 @ (272,84) | 272 each | under screen |
| Genesis | 320x224 | x2 | 640x448 @ (192,84) | 192 each | under screen |
| NES | 256x240 | x2 | 512x480 @ (256,84) | 256 each | beside the clusters |

`GbInput::buildLayout(vx, vy, vw, vh)` computes the whole pad from those four
numbers: D-pad arm = `min(80, leftMargin/3)`, A/B radius = `min(55, rightMargin/4)`,
and START/SELECT drop to the side zones when the bottom strip is under 62 px
(which is what NES's taller screen forces). Verified for all three above.

Controls carry a `shape` (`kCtlArm` / `kCtlRound` / `kCtlPill`) so `GbUi` renders
the table generically instead of hardcoding coordinates — a new console's pad is
then data, not new drawing code.

**Status: already implemented for Game Boy** as the first step of this work.

## The core abstraction

```cpp
class EmuCore {
 public:
  virtual bool begin(GbAudio *audio) = 0;
  virtual bool start(const String &romPath, const String &savePath) = 0;
  virtual void stop() = 0;
  virtual void runFrame(bool draw) = 0;
  virtual void setPad(uint32_t buttons) = 0;
  virtual const uint16_t *framebuffer() const = 0;
  virtual void frameSize(int16_t &w, int16_t &h) const = 0;
  virtual bool saveState(const String &p) = 0;
  virtual bool loadState(const String &p) = 0;
  virtual bool sramDirty() const = 0;
  virtual bool save() = 0;
};
```

Lifecycle-shaped rather than strictly frame-shaped, so the NES implementation can
satisfy it by running `nofrendo_start()` on its own task while `runFrame()` simply
waits for the next frame that task published. GB and Genesis implement it directly.

Button bits stay the GB set plus extras (`GB_BTN_C`, `GB_BTN_MODE`) for Genesis;
each core maps them to its own pad encoding, `static_assert`ed wherever a vendored
enum is mirrored — the pattern that already caught two palette mistakes.

## Prerequisite refactors

1. **`GbVideo` → `EmuVideo`** — take (source w, h, scale, origin) instead of the
   `GB_*` macros. The scanline-strip blit already generalises: Genesis's
   320x224 framebuffer is 143 KB and belongs in PSRAM, and a strip blit reads it
   linearly, which is exactly the access pattern PSRAM handles well.
2. **`GbInput` per-system layout** — done for GB; extend the table with the
   Genesis face buttons.
3. **`GbRomStore` system tagging** — filter `.gb/.gbc/.md/.gen/.bin/.nes` and tag
   each entry with its system so the picker can badge rows and the launcher can
   pick a core.
4. **`GameBoyHost` → `GameBoyCore : EmuCore`** — mechanical; the class already
   has the right shape.

## Genesis specifics

- Vendor `gwenesis/components/gwenesis/src/` (cpus/M68K, cpus/Z80, vdp, sound,
  bus) plus the scanline loop from retro-go's `main.c`, adapted into
  `GenesisCore::runFrame()`.
- **Flash is the main budget item.** The M68K tables come in `_full` and regular
  variants; use the regular ones and measure. The current app is ~627 KB of a
  3 MB partition, so there is room — but this gets checked before going further,
  not assumed.
- **Framebuffer in PSRAM**, blitted per scanline.
- **Bring it up silent first.** Skip `ym2612_run()` entirely, confirm the video
  and input path holds frame rate, then add FM audio as its own step. This is the
  same de-risking that worked for Game Boy.
- 3-button pad first (A/B/C + START); 6-button later if wanted.

## Open risk: speed

retro-go defaults to `frameskip = 3` for Genesis on a 240 MHz ESP32. The P4 is
400 MHz dual-core RISC-V and a third party has SNES running full speed on it, so
better is expected — but **this is unknowable without building it**. If Genesis
cannot hold a playable rate, that is a legitimate outcome: the answer is frameskip
plus an honest note in the docs, never silent slowness.

## Acceptance

- `.md`/`.gen` files appear in the picker with a system badge; `.gb`/`.gbc` still
  work exactly as now.
- A Genesis game boots, renders, and responds to the touch pad.
- Swapping between a GB and a Genesis game without rebooting leaks nothing
  (watch free heap across several swaps).
- Save states and battery saves work per-system.
- All flag combos compile with `USE_GENESIS_CORE` **off by default**, so the
  proven Game Boy build is never destabilised by in-progress work.
- Measured and recorded: flash delta, free internal SRAM, observed frame rate.
