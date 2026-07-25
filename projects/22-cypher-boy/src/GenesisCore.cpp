#include "GenesisCore.h"

#if USE_GENESIS_CORE

#include <CrowPanelShared.h>
#include "GbAudio.h"
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
extern int mode_pal;
extern unsigned char *ROM_DATA;

// gwenesis's 64 KB video RAM. In the non-Game&Watch build gwenesis_vdp_mem.c
// declares `unsigned char *VRAM;` but NEVER assigns it (only the G&W target,
// which we do not compile, points it at a static array). gwenesis_vdp_reset()
// memsets it and every tile/sprite fetch reads through it, so the host MUST
// give it a real 64 KB backing store - otherwise: garbage rendering and a bad
// pointer. See src/gwenesis/VENDORED.md.
extern unsigned char *VRAM;

void gwenesis_vdp_render_config(void);
void z80_start(void);
void z80_pulse_reset(void);
void z80_run(int target);
void z80_irq_line(int value);
void gwenesis_SN76489_run(int target);
void ym2612_run(int target);
// m68k_run / m68k_set_irq / m68k_update_irq come from m68k.h above.

// --- Host-provided globals ---------------------------------------------------
// gwenesis expects the application to define these; retro-go does it in its
// gwenesis/main/main.c. They are definitions, not externs.
int system_clock;
int scan_line;
// Audio buffers. Sized for PAL (1056), not NTSC (888): a PAL game produces more
// samples per frame and an NTSC-sized buffer would be overrun. Both chips write
// MONO int16 - one value per sample, not a stereo pair.
int16_t gwenesis_sn76489_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_PAL];
int sn76489_index;
int sn76489_clock;
int16_t gwenesis_ym2612_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_PAL];
int ym2612_index;
int ym2612_clock;

// gwenesis_io.c calls this to refresh the pad. We push button state directly
// from setPad() as touches arrive, so there is nothing to poll here.
void gwenesis_io_get_buttons(void) {}

// --- Save-state backend (host-provided) --------------------------------------
// gwenesis_save_state()/gwenesis_load_state() fan out to each module, and those
// call this tagged-buffer API - which upstream does NOT implement. retro-go
// supplies it from its own main.c; this mirrors that: a flat file of
// {char key[28]; uint32_t length;} records, each followed by its payload.
typedef struct {
  char key[28];
  uint32_t length;
} gw_svar_t;

static FILE *gStateFp = NULL;
static int gStateErrors = 0;

SaveState *saveGwenesisStateOpenForRead(const char *fileName) {
  (void)fileName;  // the file is opened by GenesisCore; this is just a handle
  return (SaveState *)1;
}
SaveState *saveGwenesisStateOpenForWrite(const char *fileName) {
  (void)fileName;
  return (SaveState *)1;
}

void saveGwenesisStateSetBuffer(SaveState *state, const char *tagName, void *buffer,
                                int length) {
  (void)state;
  if (!gStateFp || length <= 0) return;
  gw_svar_t var;
  memset(&var, 0, sizeof(var));
  strncpy(var.key, tagName, sizeof(var.key) - 1);
  var.length = (uint32_t)length;
  if (fwrite(&var, sizeof(var), 1, gStateFp) != 1 ||
      fwrite(buffer, (size_t)length, 1, gStateFp) != 1) {
    gStateErrors++;
  }
}

void saveGwenesisStateGetBuffer(SaveState *state, const char *tagName, void *buffer,
                                int length) {
  (void)state;
  if (!gStateFp || length <= 0) return;
  // Modules read back in roughly the order they wrote, so scan forward from the
  // current position and wrap to the start once before giving up.
  const long initial = ftell(gStateFp);
  bool wrapped = false;
  gw_svar_t var;
  while (!wrapped || ftell(gStateFp) < initial) {
    if (fread(&var, sizeof(var), 1, gStateFp) != 1) {
      if (!wrapped) {
        fseek(gStateFp, 0, SEEK_SET);
        wrapped = true;
        continue;
      }
      break;
    }
    if (strncmp(var.key, tagName, sizeof(var.key)) == 0) {
      const size_t want = (var.length < (uint32_t)length) ? var.length : (size_t)length;
      if (fread(buffer, want, 1, gStateFp) != 1) gStateErrors++;
      return;
    }
    fseek(gStateFp, (long)var.length, SEEK_CUR);
  }
  gStateErrors++;  // key missing - the state is incomplete
}

int saveGwenesisStateGet(SaveState *state, const char *tagName) {
  int value = 0;
  saveGwenesisStateGetBuffer(state, tagName, &value, sizeof(int));
  return value;
}
void saveGwenesisStateSet(SaveState *state, const char *tagName, int value) {
  saveGwenesisStateSetBuffer(state, tagName, &value, sizeof(int));
}
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

bool GenesisCore::begin(GbAudio *audio) {
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

  // VRAM (64 KB) - see the extern note above. Read per-pixel during tile/sprite
  // fetch, so try internal SRAM first for speed, fall back to PSRAM.
  vram_ = (uint8_t *)heap_caps_malloc(0x10000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!vram_) vram_ = (uint8_t *)heap_caps_malloc(0x10000, MALLOC_CAP_SPIRAM);
  if (!vram_) {
    status_ = "genesis VRAM alloc failed";
    Logger::error("genesis", status_);
    return false;
  }
  memset(vram_, 0, 0x10000);
  VRAM = vram_;

  // Stereo interleave scratch: 1152 frames * 2 ch * 2 B = 4.6 KB, internal SRAM
  // because it is written every frame and handed straight to the I2S DMA.
  mix_ = (int16_t *)heap_caps_malloc(kMixFrames * 2 * sizeof(int16_t),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!mix_) {
    Logger::error("genesis", "audio mix buffer alloc failed; running silent");
  }

  audio_ = mix_ ? audio : nullptr;  // no scratch => stay silent rather than crash

  ready_ = true;
  status_ = audio_ ? "gwenesis ready (FM + PSG)" : "gwenesis ready (silent)";
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
  // Reset the audio accumulators for this frame. Parking a chip's clock far in
  // the future is how gwenesis disables it, which is what we do when silent.
  const bool wantAudio = (audio_ != nullptr) && !audio_->muted();
  ym2612_clock = wantAudio ? 0 : 0x1000000;
  ym2612_index = 0;
  sn76489_clock = wantAudio ? 0 : 0x1000000;
  sn76489_index = 0;

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

  // GWENESIS_AUDIO_ACCURATE == 1: the chips are stepped when the CPUs touch
  // them, so this call only fills in the tail of the frame.
  if (wantAudio) {
    gwenesis_SN76489_run(system_clock);
    ym2612_run(system_clock);
    submitAudio_();
  }

  m68k.cycles -= system_clock;

  frames_++;
}

uint32_t GenesisCore::sampleRate() const {
  // mode_pal is only meaningful once a ROM is running; NTSC is the right default
  // and the launcher re-applies this on every launch.
  return mode_pal ? GWENESIS_AUDIO_FREQ_PAL : GWENESIS_AUDIO_FREQ_NTSC;
}

void GenesisCore::submitAudio_() {
  if (!audio_ || !mix_) return;
  // Both chips emit MONO. Take the shorter run so neither buffer is read past
  // what it actually produced this frame, sum them, and interleave to stereo.
  //
  // retro-go submits only the YM2612 and leaves "TODO: Mix in
  // gwenesis_sn76489_buffer" - so it silently drops the PSG, which on Genesis
  // carries a lot of the percussion and effects. Mixing both is a real
  // improvement over upstream, not just parity.
  int n = ym2612_index < sn76489_index ? ym2612_index : sn76489_index;
  if (n <= 0) return;
  if (n > (int)kMixFrames) n = (int)kMixFrames;

  for (int i = 0; i < n; i++) {
    // Attenuate before summing: two independently full-scale chips would clip
    // constantly at unity. 3/4 on FM (which carries the music) and 1/2 on the
    // PSG keeps the mix clear of the rails without sounding quiet.
    int32_t v = ((int32_t)gwenesis_ym2612_buffer[i] * 3) / 4 +
                ((int32_t)gwenesis_sn76489_buffer[i] / 2);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    const int16_t s16 = (int16_t)v;
    mix_[i * 2] = s16;
    mix_[i * 2 + 1] = s16;
  }
  audio_->submit(mix_, (size_t)n * 2);
}

bool GenesisCore::saveState(const String &path) {
  if (!romLoaded_ || path.length() == 0) return false;
  gStateFp = fopen(path.c_str(), "wb");
  if (!gStateFp) {
    Logger::error("genesis", String("state open failed: ") + path);
    return false;
  }
  gStateErrors = 0;
  gwenesis_save_state();
  fclose(gStateFp);
  gStateFp = NULL;
  if (gStateErrors) {
    Logger::error("genesis", String("state saved with ") + gStateErrors + " write errors");
    return false;
  }
  return true;
}

bool GenesisCore::loadState(const String &path) {
  if (!romLoaded_ || path.length() == 0) return false;
  gStateFp = fopen(path.c_str(), "rb");
  if (!gStateFp) return false;  // no state in that slot yet - not an error
  gStateErrors = 0;
  gwenesis_load_state();
  fclose(gStateFp);
  gStateFp = NULL;
  if (gStateErrors) {
    // A partially-applied state leaves the machine inconsistent, so say so
    // rather than letting it run into undefined behaviour unannounced.
    Logger::error("genesis", String("state load: ") + gStateErrors + " keys missing/short");
    return false;
  }
  return true;
}

bool GenesisCore::saveThumb(const String &statePath) {
#if USE_GB_SD
  if (!romLoaded_ || !fb_ || statePath.length() == 0) return false;
  // Same 40x36 thumbnail the Game Boy slots use, so the pause overlay renders
  // every core's slots identically. Genesis is paletted, so each sampled pixel
  // goes through CRAM565 on the way out.
  static uint16_t thumb[40 * 36];
  const uint16_t *pal = palette();
  if (!pal) return false;
  const int16_t sw = frameW_, sh = frameH_;
  for (int16_t ty = 0; ty < 36; ty++) {
    for (int16_t tx = 0; tx < 40; tx++) {
      // Point-sample rather than box-filter: the source is palette indices, so
      // averaging them would blend unrelated colours instead of pixels.
      const int16_t sx = (int16_t)((int32_t)tx * sw / 40);
      const int16_t sy = (int16_t)((int32_t)ty * sh / 36);
      thumb[ty * 40 + tx] = pal[fb_[(size_t)sy * kStride + sx]];
    }
  }
  FILE *f = fopen((statePath + ".thm").c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(thumb, 1, sizeof(thumb), f) == sizeof(thumb);
  fclose(f);
  return ok;
#else
  (void)statePath;
  return false;
#endif
}

#endif  // USE_GENESIS_CORE
