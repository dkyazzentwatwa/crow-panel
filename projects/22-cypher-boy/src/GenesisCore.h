#ifndef CYPHER_BOY_GENESIS_CORE_H
#define CYPHER_BOY_GENESIS_CORE_H

#include "../config/ProjectConfig.h"
#include "EmuCore.h"
#include <Arduino.h>

#if USE_GENESIS_CORE

// Sega Genesis / Mega Drive on the vendored gwenesis core.
//
// gwenesis is callee-style like gnuboy - it exposes m68k_run/z80_run/
// gwenesis_vdp_render_line and the host owns the loop - so runFrame() drives a
// scanline loop adapted from retro-go's gwenesis/main/main.c rather than
// handing control away. That is exactly why Genesis came before NES.
//
// Output is 8-bit palette indices plus a 256-entry RGB565 LUT (CRAM565,
// byte-swapped), not direct RGB565, so this reports itself through
// framebuffer8() + palette() and GbVideo does the lookup while expanding each
// scanline.
//
// v1 is SILENT on purpose: ym2612_run() and gwenesis_SN76489_run() are left out
// of the loop so the video and input path can be proven before FM synthesis -
// the same de-risking that made the Game Boy bring-up go cleanly.
class GenesisCore : public EmuCore {
 public:
  // 320x224 NTSC / 320x240 PAL; the buffer is allocated for the larger.
  static const int16_t kMaxW = 320;
  static const int16_t kMaxH = 240;
  // gwenesis hardcodes the row stride to 320 (`&screen_buffer[line * 320]`)
  // even in H32/256 mode, so the buffer is always 320 wide regardless of how
  // much of it is visible.
  static const int16_t kStride = 320;
  // ...and in H32 mode it does `memset(screen_buffer_line - (320-256)/2, ...)`,
  // which writes 32 bytes BEFORE the buffer on line 0. At boot the VDP regs are
  // zeroed, so H32 is the *initial* mode and that underflow corrupts the heap
  // immediately. The allocation carries a guard margin at both ends and the
  // pointer handed to gwenesis is offset into it.
  static const int16_t kGuard = 64;
  int16_t stride() const { return kStride; }
  uint32_t sampleRate() const override;

  EmuSystem system() const override { return kSysGenesis; }
  const char *name() const override { return "Genesis"; }
  int16_t frameW() const override { return frameW_; }
  int16_t frameH() const override { return frameH_; }
  uint8_t scale() const override { return 2; }  // 640x448 leaves room for the pad

  bool begin(GbAudio *audio) override;
  bool start(const String &romPath, const String &savePath) override;
  void stop() override;
  void runFrame(bool draw) override;
  void setPad(uint32_t buttons) override;

  const uint16_t *framebuffer() const override { return nullptr; }
  const uint8_t *framebuffer8() const override { return fb_; }
  // CRAM565 directly - no copy and no byte swap.
  //
  // gwenesis packs each entry as red->bits 13-15, green->8-10, blue->2-4, which
  // IS native RGB565. retro-go byte-swaps it only because its SPI panel wants
  // big-endian; draw16bitRGBBitmap on this DSI panel takes native little-endian
  // (the same order the Game Boy path already renders correctly). Swapping here
  // trades red and blue - which is exactly the blue cast it produced.
  const uint16_t *palette() const override;

  // Cartridge battery saves are NOT possible with this core: gwenesis's memory
  // map (enum mapped_address in gwenesis_bus.h) has ROM / Z80 RAM / VDP / IO /
  // work RAM and no SRAM region at all, so a game's save hardware simply is not
  // emulated. Save STATES cover the need better anyway - they snapshot the whole
  // machine and can be taken anywhere, not just at the game's own save points.
  bool sramDirty() const override { return false; }
  bool save() override { return false; }
  void tickSave(uint32_t) override {}
  bool saveState(const String &path) override;
  bool loadState(const String &path) override;
  bool saveThumb(const String &statePath) override;

 private:
  void submitAudio_();

 public:

  bool ready() const override { return ready_; }
  bool romLoaded() const override { return romLoaded_; }
  uint32_t frameCount() const override { return frames_; }
  const String &status() const override { return status_; }

 private:
  uint8_t *fbAlloc_ = nullptr;  // raw allocation incl. guard margins
  uint8_t *fb_ = nullptr;       // fbAlloc_ + kGuard: what gwenesis is given
  uint8_t *vram_ = nullptr;     // 64 KB backing store gwenesis's VRAM points at
  uint8_t *rom_ = nullptr;   // whole cartridge, PSRAM
  class GbAudio *audio_ = nullptr;  // null => silent
  int16_t *mix_ = nullptr;   // stereo interleave scratch, internal SRAM
  static const size_t kMixFrames = 1152;  // >= PAL 1056, headroom
  size_t romSize_ = 0;
  int16_t frameW_ = 320, frameH_ = 224;
  uint32_t pad_ = 0;
  uint32_t frames_ = 0;
  bool ready_ = false;
  bool romLoaded_ = false;
  String status_;
};

#endif  // USE_GENESIS_CORE
#endif
