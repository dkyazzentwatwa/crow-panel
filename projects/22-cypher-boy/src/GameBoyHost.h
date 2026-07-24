#ifndef CYPHER_BOY_HOST_H
#define CYPHER_BOY_HOST_H

#include "../config/ProjectConfig.h"
#include "EmuCore.h"
#include <Arduino.h>

class GbAudio;

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
class GameBoyHost : public EmuCore {
 public:
  EmuSystem system() const override { return kSysGameBoy; }
  const char *name() const override { return "Game Boy"; }
  int16_t frameW() const override { return GB_W; }
  int16_t frameH() const override { return GB_H; }
  uint8_t scale() const override { return GB_SCALE; }

  // Pass an audio sink to get sound; nullptr runs silent, which is exactly what
  // USE_GB_AUDIO=0 builds do.
  bool begin(GbAudio *audio) override;
  bool start(const String &romPath, const String &savePath) override;
  void stop() override;
  void runFrame(bool draw) override;
  void setPad(uint32_t buttons) override;

  // Kept so existing call sites and the selftest read naturally; start() is the
  // interface spelling of the same thing.
  bool loadRom(const String &romPath, const String &savePath) { return start(romPath, savePath); }

  // Battery save (cartridge SRAM) handling.
  bool sramDirty() const override;
  bool save() override;                       // force a write now
  void tickSave(uint32_t nowMs) override;     // debounced autosave; call each frame

  // Save states (Delta-style slots). Separate from the battery save: a state
  // is a full machine snapshot, the battery save is just cartridge SRAM.
  bool saveState(const String &path) override;
  bool loadState(const String &path) override;

  // Thumbnails: the live 160x144 frame downscaled by kThumbScale and written
  // beside the state file, so the pause overlay can show what each slot holds.
  static const uint8_t kThumbScale = 4;
  static const int16_t kThumbW = GB_W / kThumbScale;   // 40
  static const int16_t kThumbH = GB_H / kThumbScale;   // 36
  bool saveThumb(const String &statePath) override;
  // Reads into `out` (kThumbW*kThumbH uint16). False if there is no thumbnail.
  static bool loadThumb(const String &statePath, uint16_t *out);
  static String thumbPath(const String &statePath) { return statePath + ".thm"; }

  const uint16_t *framebuffer() const override { return fb_; }
  bool ready() const override { return ready_; }
  bool romLoaded() const override { return romLoaded_; }
  uint32_t frameCount() const override { return frames_; }
  const String &status() const override { return status_; }

 private:
  uint16_t *fb_ = nullptr;      // GB_W*GB_H RGB565, internal SRAM
  int16_t *soundScratch_ = nullptr;  // gnuboy's mixer target
  GbAudio *audio_ = nullptr;         // null => silent
  String savePath_;
  String status_;
  bool ready_ = false;
  bool romLoaded_ = false;
  uint32_t frames_ = 0;
  uint32_t lastDirtyMs_ = 0;
  bool pendingSave_ = false;
};

#endif
