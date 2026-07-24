#ifndef CYPHER_BOY_HOST_H
#define CYPHER_BOY_HOST_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Game Boy button bits. These intentionally mirror gnuboy's gb_padbtn_t
// values one-for-one; GameBoyHost.cpp static_asserts that they still match the
// vendored core, so a future gnuboy bump cannot silently rewire the gamepad.
enum GbButton : uint32_t {
  GB_BTN_RIGHT  = 0x01,
  GB_BTN_LEFT   = 0x02,
  GB_BTN_UP     = 0x04,
  GB_BTN_DOWN   = 0x08,
  GB_BTN_A      = 0x10,
  GB_BTN_B      = 0x20,
  GB_BTN_SELECT = 0x40,
  GB_BTN_START  = 0x80,
};

// Owns the vendored gnuboy core. This is the ONLY translation unit that
// includes gnuboy.h, so the emulator stays behind a small C++ surface that the
// loop and the serial commands both drive (keeping touch and serial in parity).
//
// Video: gnuboy renders scanlines straight into our framebuffer during
// gnuboy_run(true), so the caller blits framebuffer() afterwards - no video
// callback is registered. (Drawing inside the callback is only required for
// GB_PIXEL_PALETTED; we use RGB565.)
//
// Audio: v1 is silent. gnuboy gets a valid samplerate and a small throwaway
// sound buffer but a NULL audio callback, so the mixer runs and emits nothing.
// Passing samplerate 0 would be undefined behaviour - see src/gnuboy/VENDORED.md.
class GameBoyHost {
 public:
  bool begin();
  bool loadRom(const String &romPath, const String &savePath);
  void runFrame(bool draw);
  void setPad(uint32_t buttons);

  // Battery save (cartridge SRAM) handling.
  bool sramDirty() const;
  bool save();                       // force a write now
  void tickSave(uint32_t nowMs);     // debounced autosave; call each frame

  const uint16_t *framebuffer() const { return fb_; }
  bool ready() const { return ready_; }
  bool romLoaded() const { return romLoaded_; }
  uint32_t frameCount() const { return frames_; }
  const String &status() const { return status_; }

 private:
  uint16_t *fb_ = nullptr;      // GB_W*GB_H RGB565, internal SRAM
  int16_t *soundScratch_ = nullptr;  // throwaway; keeps the mixer off NULL
  String savePath_;
  String status_;
  bool ready_ = false;
  bool romLoaded_ = false;
  uint32_t frames_ = 0;
  uint32_t lastDirtyMs_ = 0;
  bool pendingSave_ = false;
};

#endif
