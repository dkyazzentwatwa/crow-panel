#include "NesCore.h"

#if USE_NES_CORE

#include <CrowPanelShared.h>
#include "esp_heap_caps.h"

extern "C" {
#include "nofrendo/nes.h"
#include "nofrendo/input.h"
#include "nofrendo/nofrendo.h"
#include "nofrendo/state.h"
}

namespace {
// nofrendo's pad is a plain bitmask, but its bit order is its own - A is 0x01
// and the directions live in the HIGH nibble, the reverse of the Game Boy
// layout our GbButton bits use. Mapped explicitly rather than by arithmetic so
// the difference stays visible (the same discipline used for Genesis, whose pad
// is an index rather than a mask).
struct PadMap {
  uint32_t ours;
  int nes;
};
const PadMap kPadMap[] = {
    {GB_BTN_UP, NES_PAD_UP},         {GB_BTN_DOWN, NES_PAD_DOWN},
    {GB_BTN_LEFT, NES_PAD_LEFT},     {GB_BTN_RIGHT, NES_PAD_RIGHT},
    {GB_BTN_A, NES_PAD_A},           {GB_BTN_B, NES_PAD_B},
    {GB_BTN_SELECT, NES_PAD_SELECT}, {GB_BTN_START, NES_PAD_START},
};
const size_t kPadMapCount = sizeof(kPadMap) / sizeof(kPadMap[0]);
}  // namespace

bool NesCore::begin(GbAudio *) {
  if (ready_) return true;

  // 272x240 palette indices = 65,280 B. PSRAM: the scanline blitter reads it
  // linearly, which is the access pattern PSRAM handles well, and internal SRAM
  // is already committed elsewhere.
  fb_ = (uint8_t *)heap_caps_malloc(kFbBytes, MALLOC_CAP_SPIRAM);
  if (!fb_) {
    status_ = "NES framebuffer alloc failed";
    Logger::error("nes", status_);
    return false;
  }
  memset(fb_, 0, kFbBytes);
  // Skip the overscan rows and the 8-pixel left overdraw so the blitter sees
  // only visible picture; stride() still reports the full 272 pitch.
  fbVisible_ = fb_ + (size_t)kOverscanY * kPitch + NES_SCREEN_OVERDRAW;

  // nes_init() allocates all emulator state itself - unlike gwenesis there are
  // no host-provided globals and no unassigned pointers to back.
  // SYS_DETECT lets nofrendo pick NTSC/PAL from the ROM header itself.
  if (nes_init(SYS_DETECT, GB_SAMPLERATE, false, nullptr) == nullptr) {
    status_ = "nes_init failed";
    Logger::error("nes", status_);
    return false;
  }

  // 256-entry RGB565 LUT. Native little-endian - do NOT byte-swap it; that is
  // exactly what gave the Genesis picture a blue cast.
  pal_ = (uint16_t *)nofrendo_buildpalette(NES_PALETTE_PVM, 16);
  if (!pal_) {
    status_ = "NES palette build failed";
    Logger::error("nes", status_);
    return false;
  }

  ready_ = true;
  status_ = "nofrendo ready (silent)";
  Logger::info("nes", status_);
  return true;
}

bool NesCore::start(const String &romPath, const String &savePath) {
  (void)savePath;  // cartridge SRAM is not wired yet
  if (!ready_ && !begin(nullptr)) return false;
  if (romLoaded_) stop();

  // nofrendo does its own file I/O (like gnuboy, unlike gwenesis which takes a
  // buffer), so it wants the full VFS path.
  if (nes_loadfile(romPath.c_str()) < 0) {
    status_ = String("ROM load failed: ") + romPath;
    Logger::error("nes", status_);
    return false;
  }

  romLoaded_ = true;
  frames_ = 0;

  // nes_loadfile() -> nes_insertcart() -> nes_reset() leaves scanline at 241, so
  // the FIRST emulated frame covers only lines 241-261. Burn two undrawn frames
  // so the first frame we actually blit is whole. (retro-go does the same and
  // notes it is also needed for save-state restore to work.)
  nes_setvidbuf(fb_);
  nes_emulate(false);
  nes_emulate(false);

  status_ = String("loaded ") + romPath;
  Logger::info("nes", status_);
  return true;
}

void NesCore::stop() {
  if (!romLoaded_) return;
  romLoaded_ = false;
  nes_shutdown();  // frees the ROM image and all core-side state
  ready_ = false;  // nes_init() must run again before the next cart
  status_ = "stopped";
}

void NesCore::setPad(uint32_t buttons) {
  if (!romLoaded_) return;
  int nes = 0;
  for (size_t i = 0; i < kPadMapCount; i++) {
    if (buttons & kPadMap[i].ours) nes |= kPadMap[i].nes;
  }
  input_update(0, nes);
}

void NesCore::runFrame(bool draw) {
  // nes.mapper and nes.cart are dereferenced unguarded inside nes_emulate(), so
  // running before a cart is inserted is a null-deref reboot.
  if (!romLoaded_) return;

  // Re-arm EVERY frame. nes_reset() sets nes.vidbuf = NULL, and a reset can be
  // triggered by a cart insert or a failed state load - after which nes_emulate
  // silently does `draw = draw && nes.vidbuf != NULL` and renders nothing, with
  // no error at all. A permanently black screen with no diagnostic is a nasty
  // way to lose an afternoon, and this one pointer store prevents it.
  nes_setvidbuf(fb_);
  nes_emulate(draw);
  frames_++;
}

bool NesCore::saveState(const String &path) {
  if (!romLoaded_) return false;
  return state_save(path.c_str()) == 0;
}

bool NesCore::loadState(const String &path) {
  if (!romLoaded_) return false;
  const bool ok = state_load(path.c_str()) == 0;
  // A failed load resets the machine, which nulls vidbuf; runFrame() re-arms it
  // next tick, but be explicit rather than relying on that.
  nes_setvidbuf(fb_);
  return ok;
}

bool NesCore::saveThumb(const String &statePath) {
  (void)statePath;
  return false;  // host-side; not wired for this core yet
}

#endif  // USE_NES_CORE
