#ifndef CYPHER_BOY_EMU_CORE_H
#define CYPHER_BOY_EMU_CORE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

class GbAudio;

// Shared pad vocabulary. Values mirror gnuboy's gb_padbtn_t one-for-one (which
// GameBoyHost.cpp static_asserts), and every other core maps them to its own
// encoding - Genesis's pad enum, for instance, is an index rather than a mask.
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

enum EmuSystem : uint8_t {
  kSysGameBoy = 0,
  kSysGenesis,
  kSysNes,
  kSysCount,  // also "unrecognised"
};

// One emulated console, behind a stable seam.
//
// Deliberately LIFECYCLE-shaped rather than strictly frame-shaped. gnuboy and
// gwenesis are both callee-style - the host owns the loop and calls into them
// per frame - so they implement runFrame() directly. nofrendo (NES) is
// caller-style: nofrendo_start() blocks and calls *your* callbacks. It can still
// satisfy this interface by running that loop on its own task, with runFrame()
// waiting on the frame that task published. Designing the seam this way now is
// what stops the NES port from forcing a rewrite later.
//
// See docs/superpowers/specs/2026-07-24-cypher-boy-multi-system-design.md.
class EmuCore {
 public:
  virtual ~EmuCore() {}

  virtual EmuSystem system() const = 0;
  virtual const char *name() const = 0;

  virtual bool begin(GbAudio *audio) = 0;
  // romPath/savePath are full VFS paths (stdio), because the cores do their own
  // file I/O - see the path-namespace note in TECHNICAL.md.
  virtual bool start(const String &romPath, const String &savePath) = 0;
  virtual void stop() = 0;

  virtual void runFrame(bool draw) = 0;
  virtual void setPad(uint32_t buttons) = 0;

  // Native output. The host centres and integer-scales this; it never assumes
  // a particular console's dimensions.
  // Direct RGB565 output (Game Boy). Paletted cores return nullptr here and
  // provide framebuffer8() + palette() instead - the Genesis VDP renders 8-bit
  // colour indices, so converting to RGB565 in a second full buffer would cost
  // 143 KB and a pass over every pixel for nothing. The blitter already reads
  // each pixel to expand it, so the lookup rides along for free there.
  virtual const uint16_t *framebuffer() const = 0;
  virtual const uint8_t *framebuffer8() const { return nullptr; }
  virtual const uint16_t *palette() const { return nullptr; }  // 256 entries
  virtual int16_t frameW() const = 0;
  virtual int16_t frameH() const = 0;
  virtual uint8_t scale() const = 0;
  // Row pitch of the framebuffer. Equals frameW() for most cores; the Genesis
  // VDP always writes rows 320 apart even when showing 256 pixels.
  virtual int16_t stride() const { return frameW(); }

  // Battery save (cartridge SRAM) - distinct from a save state.
  virtual bool sramDirty() const = 0;
  virtual bool save() = 0;
  virtual void tickSave(uint32_t nowMs) = 0;

  // Save states: a full machine snapshot, plus a thumbnail of the moment.
  virtual bool saveState(const String &path) = 0;
  virtual bool loadState(const String &path) = 0;
  virtual bool saveThumb(const String &statePath) = 0;

  virtual bool ready() const = 0;
  virtual bool romLoaded() const = 0;
  virtual uint32_t frameCount() const = 0;
  virtual const String &status() const = 0;
};

#endif
