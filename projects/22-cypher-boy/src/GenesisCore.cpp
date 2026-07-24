#include "GenesisCore.h"

#if USE_GENESIS_CORE

#include <CrowPanelShared.h>
#include <stdio.h>
#include "esp_heap_caps.h"

extern "C" {
#include "gwenesis/gwenesis_bus.h"
#include "gwenesis/gwenesis_io.h"
#include "gwenesis/gwenesis_savestate.h"
#include "gwenesis/gwenesis_vdp.h"
#include "gwenesis/m68k.h"

// Globals gwenesis expects the host to drive. Declared here rather than pulled
// from a header because upstream defines several of them in .c files only.
extern int screen_width, screen_height;
extern int zclk;
extern unsigned char gwenesis_vdp_regs[0x20];
extern unsigned int gwenesis_vdp_status;
extern unsigned short CRAM565[256];
extern int hint_pending;
extern unsigned char *ROM_DATA;

void gwenesis_vdp_render_config(void);
void z80_start(void);
void z80_pulse_reset(void);
void z80_run(int target);
void z80_irq_line(int value);
// m68k_run / m68k_set_irq / m68k_update_irq come from m68k.h above.

// --- Host-provided globals ---------------------------------------------------
// gwenesis expects the application to define these; retro-go does it in its
// gwenesis/main/main.c. They are definitions, not externs.
int system_clock;
int scan_line;
// Audio buffers must exist even in the silent build: ym2612.c and
// gwenesis_sn76489.c reference them at file scope, so leaving them out is a
// link error rather than a saving. One NTSC frame is 888 samples.
int16_t gwenesis_sn76489_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_NTSC];
int sn76489_index;
int sn76489_clock;
int16_t gwenesis_ym2612_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_NTSC];
int ym2612_index;
int ym2612_clock;

// gwenesis_io.c calls this to refresh the pad. We push button state directly
// from setPad() as touches arrive, so there is nothing to poll here.
void gwenesis_io_get_buttons(void) {}
}

namespace {
// gwenesis's pad enum is an INDEX (0-7), not a bitmask, and its face-button
// order is B, C, A - not A, B, C. Mapping our bits by hand rather than by
// arithmetic keeps that surprise in one visible place.
struct PadMap {
  uint32_t bit;
  int gwButton;
};
const PadMap kPadMap[] = {
    {GB_BTN_UP, PAD_UP},       {GB_BTN_DOWN, PAD_DOWN},
    {GB_BTN_LEFT, PAD_LEFT},   {GB_BTN_RIGHT, PAD_RIGHT},
    {GB_BTN_A, PAD_A},         {GB_BTN_B, PAD_B},
    {GB_BTN_START, PAD_S},
    // C has no Game Boy equivalent; SELECT stands in until the pad layout grows
    // a third face button for this system.
    {GB_BTN_SELECT, PAD_C},
};
const size_t kPadMapCount = sizeof(kPadMap) / sizeof(kPadMap[0]);
}  // namespace

const uint16_t *GenesisCore::palette() const {
  // The 8-bit framebuffer indexes 0-255 straight into CRAM565's first 256
  // entries (4 x 64: normal, shadow, highlight variants).
  return (const uint16_t *)CRAM565;
}

bool GenesisCore::begin(GbAudio *) {
  if (ready_) return true;

  // 8-bit indices, up to 320x240 = 75 KB. PSRAM: internal SRAM is already
  // committed to the GB framebuffer, the video strip and the audio scratch,
  // and gwenesis's own bss is another 130 KB.
  const size_t fbBytes = (size_t)kStride * kMaxH + 2 * kGuard;
  fbAlloc_ = (uint8_t *)heap_caps_malloc(fbBytes, MALLOC_CAP_SPIRAM);
  if (!fbAlloc_) {
    status_ = "genesis framebuffer alloc failed";
    Logger::error("genesis", status_);
    return false;
  }
  memset(fbAlloc_, 0, fbBytes);
  fb_ = fbAlloc_ + kGuard;  // leaves room for gwenesis's H32 underflow memset

  ready_ = true;
  status_ = "gwenesis ready (silent)";
  Logger::info("genesis", status_);
  return true;
}

bool GenesisCore::start(const String &romPath, const String &savePath) {
  (void)savePath;  // cartridge SRAM is not wired yet
  if (!ready_ && !begin(nullptr)) return false;
  if (romLoaded_) stop();

  // Unlike gnuboy, gwenesis does no file I/O of its own: load_cartridge() takes
  // a buffer, so the whole ROM is read here. Genesis carts are 512 KB - 4 MB,
  // which is nothing against 32 MB of PSRAM.
  FILE *f = fopen(romPath.c_str(), "rb");
  if (!f) {
    status_ = String("ROM open failed: ") + romPath;
    Logger::error("genesis", status_);
    return false;
  }
  fseek(f, 0, SEEK_END);
  const long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 8 * 1024 * 1024) {
    fclose(f);
    status_ = String("implausible ROM size: ") + sz;
    Logger::error("genesis", status_);
    return false;
  }
  rom_ = (uint8_t *)heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
  if (!rom_) {
    fclose(f);
    status_ = "ROM alloc failed (PSRAM)";
    Logger::error("genesis", status_);
    return false;
  }
  const size_t got = fread(rom_, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) {
    heap_caps_free(rom_);
    rom_ = nullptr;
    status_ = "ROM read short";
    Logger::error("genesis", status_);
    return false;
  }
  romSize_ = (size_t)sz;

  load_cartridge(rom_, romSize_);
  power_on();
  reset_emulation();
  z80_start();
  gwenesis_vdp_reset();

  romLoaded_ = true;
  frames_ = 0;
  status_ = String("loaded ") + romPath + " (" + (romSize_ / 1024) + " KB)";
  Logger::info("genesis", status_);
  return true;
}

void GenesisCore::stop() {
  if (!romLoaded_) return;
  romLoaded_ = false;
  if (rom_) {
    heap_caps_free(rom_);  // load_cartridge() does not take ownership here
    rom_ = nullptr;
    romSize_ = 0;
  }
  status_ = "stopped";
}

void GenesisCore::setPad(uint32_t buttons) {
  pad_ = buttons;
  if (!romLoaded_) return;
  for (size_t i = 0; i < kPadMapCount; i++) {
    if (buttons & kPadMap[i].bit) {
      gwenesis_io_pad_press_button(0, kPadMap[i].gwButton);
    } else {
      gwenesis_io_pad_release_button(0, kPadMap[i].gwButton);
    }
  }
}

void GenesisCore::runFrame(bool draw) {
  if (!romLoaded_) return;

  // Scanline loop adapted from retro-go's gwenesis/main/main.c. The audio calls
  // are deliberately absent - see the class comment.
  const int linesPerFrame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
  int hintCounter = gwenesis_vdp_regs[10];

  screen_width = REG12_MODE_H40 ? 320 : 256;
  screen_height = REG1_PAL ? 240 : 224;
  frameW_ = (int16_t)screen_width;
  frameH_ = (int16_t)screen_height;

  gwenesis_vdp_set_buffer((unsigned short *)fb_);
  gwenesis_vdp_render_config();

  system_clock = 0;
  zclk = 0;
  scan_line = 0;

  while (scan_line < linesPerFrame) {
    m68k_run(system_clock + VDP_CYCLES_PER_LINE);
    z80_run(system_clock + VDP_CYCLES_PER_LINE);

    if (draw && scan_line < screen_height) {
      gwenesis_vdp_render_line(scan_line);
    }

    if ((scan_line == 0) || (scan_line > screen_height)) {
      hintCounter = REG10_LINE_COUNTER;
    }
    if (--hintCounter < 0) {
      if ((REG0_LINE_INTERRUPT != 0) && (scan_line <= screen_height)) {
        hint_pending = 1;
        if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0) m68k_update_irq(4);
      }
      hintCounter = REG10_LINE_COUNTER;
    }

    scan_line++;

    if (scan_line == screen_height) {
      if (REG1_VBLANK_INTERRUPT != 0) {
        gwenesis_vdp_status |= STATUS_VIRQPENDING;
        m68k_set_irq(6);
      }
      z80_irq_line(1);
    }
    if (scan_line == (screen_height + 1)) {
      z80_irq_line(0);
    }

    system_clock += VDP_CYCLES_PER_LINE;
  }

  m68k.cycles -= system_clock;

  frames_++;
}

bool GenesisCore::saveState(const String &path) {
  if (!romLoaded_) return false;
  // gwenesis's savestate module is stream-based upstream; wiring it to a file
  // is deliberately left until the core is proven to run at all.
  (void)path;
  Logger::error("genesis", "save states not wired yet for Genesis");
  return false;
}

bool GenesisCore::loadState(const String &path) {
  (void)path;
  return false;
}

bool GenesisCore::saveThumb(const String &statePath) {
  (void)statePath;
  return false;
}

#endif  // USE_GENESIS_CORE
