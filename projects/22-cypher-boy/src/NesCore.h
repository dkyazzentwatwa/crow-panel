#ifndef CYPHER_BOY_NES_CORE_H
#define CYPHER_BOY_NES_CORE_H

#include "../config/ProjectConfig.h"
#include "EmuCore.h"
#include <Arduino.h>

#if USE_NES_CORE

// Nintendo Entertainment System on the vendored nofrendo core.
//
// nofrendo is FRAME-DRIVEN like gnuboy and gwenesis: `nes_emulate(bool draw)`
// runs exactly one frame and returns, so it drops straight into runFrame() with
// no FreeRTOS task and no cross-task frame marshalling. (The multi-system design
// originally assumed otherwise - see src/nofrendo/VENDORED.md for why that was
// wrong and how it was confirmed.)
//
// Output is 8-bit palette indices plus a 256-entry RGB565 LUT, the same
// framebuffer8() + palette() path Genesis already uses.
//
// v1 is SILENT on purpose - audio is a later step, exactly as Game Boy and
// Genesis were brought up.
class NesCore : public EmuCore {
 public:
  // NES_SCREEN_PITCH is 8 + 256 + 8: the renderer uses 8 pixels of overdraw on
  // each side and WILL write into them, so the buffer must be 272 wide even
  // though only 256 are shown. A 256-wide buffer corrupts memory every frame.
  static const int16_t kPitch = 272;
  static const int16_t kVisibleW = 256;
  static const int16_t kFullH = 240;
  // Trim the 8 top/bottom scanlines most NTSC sets hid; 224 also matches the
  // Genesis viewport height, so the pad layout lands in the same place.
  static const int16_t kOverscanY = 8;
  static const int16_t kVisibleH = 224;
  static const size_t kFbBytes = (size_t)kPitch * kFullH;  // 65280

  EmuSystem system() const override { return kSysNes; }
  const char *name() const override { return "NES"; }
  int16_t frameW() const override { return kVisibleW; }
  int16_t frameH() const override { return kVisibleH; }
  int16_t stride() const override { return kPitch; }
  uint8_t scale() const override { return 2; }  // 512x448, same as Genesis

  bool begin(GbAudio *audio) override;
  bool start(const String &romPath, const String &savePath) override;
  void stop() override;
  void runFrame(bool draw) override;
  void setPad(uint32_t buttons) override;

  const uint16_t *framebuffer() const override { return nullptr; }
  const uint8_t *framebuffer8() const override { return fbVisible_; }
  const uint16_t *palette() const override { return pal_; }

  bool sramDirty() const override { return false; }  // cart SRAM: not yet wired
  bool save() override { return false; }
  void tickSave(uint32_t) override {}
  bool saveState(const String &path) override;
  bool loadState(const String &path) override;
  bool saveThumb(const String &statePath) override;

  bool ready() const override { return ready_; }
  bool romLoaded() const override { return romLoaded_; }
  uint32_t frameCount() const override { return frames_; }
  const String &status() const override { return status_; }

 private:
  uint8_t *fb_ = nullptr;         // full 272x240 buffer handed to nofrendo
  uint8_t *fbVisible_ = nullptr;  // fb_ + overscan rows + left overdraw
  uint16_t *pal_ = nullptr;       // 256-entry RGB565, allocated by the core
  uint32_t frames_ = 0;
  bool ready_ = false;
  bool romLoaded_ = false;
  String status_;
};

#endif  // USE_NES_CORE
#endif
