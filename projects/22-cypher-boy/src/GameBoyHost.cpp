#include "GameBoyHost.h"

#include <CrowPanelShared.h>
#include "esp_heap_caps.h"

extern "C" {
#include "gnuboy/gnuboy.h"
}

// Our public button bits must stay identical to the vendored core's, since
// setPad() forwards them straight through. If a gnuboy bump ever renumbers
// gb_padbtn_t this fails the build instead of silently swapping A and B.
static_assert((uint32_t)GB_BTN_RIGHT  == (uint32_t)GB_PAD_RIGHT,  "pad bit drift: RIGHT");
static_assert((uint32_t)GB_BTN_LEFT   == (uint32_t)GB_PAD_LEFT,   "pad bit drift: LEFT");
static_assert((uint32_t)GB_BTN_UP     == (uint32_t)GB_PAD_UP,     "pad bit drift: UP");
static_assert((uint32_t)GB_BTN_DOWN   == (uint32_t)GB_PAD_DOWN,   "pad bit drift: DOWN");
static_assert((uint32_t)GB_BTN_A      == (uint32_t)GB_PAD_A,      "pad bit drift: A");
static_assert((uint32_t)GB_BTN_B      == (uint32_t)GB_PAD_B,      "pad bit drift: B");
static_assert((uint32_t)GB_BTN_SELECT == (uint32_t)GB_PAD_SELECT, "pad bit drift: SELECT");
static_assert((uint32_t)GB_BTN_START  == (uint32_t)GB_PAD_START,  "pad bit drift: START");

// Frames of quiet after the last SRAM change before the battery save is
// flushed to SD. Long enough that a save-heavy moment doesn't thrash the card,
// short enough that yanking power shortly after saving in-game keeps progress.
static const uint32_t kSaveDebounceMs = 2000;

// Small scratch buffer so gnuboy's mixer always has somewhere valid to write.
// It is never handed to a callback (there is none), so its contents are discarded.
static const size_t kSoundScratchSamples = 1024;

bool GameBoyHost::begin() {
  if (ready_) return true;

  // The GB framebuffer is only 45 KB and is touched every frame, so it stays
  // in internal SRAM. (Cartridge ROM/RAM go to PSRAM - see gnuboy/VENDORED.md.)
  fb_ = (uint16_t *)heap_caps_malloc(GB_W * GB_H * sizeof(uint16_t),
                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!fb_) {
    status_ = "framebuffer alloc failed";
    Logger::error("gb", status_);
    return false;
  }
  memset(fb_, 0, GB_W * GB_H * sizeof(uint16_t));

  soundScratch_ = (int16_t *)heap_caps_malloc(kSoundScratchSamples * sizeof(int16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!soundScratch_) {
    status_ = "sound scratch alloc failed";
    Logger::error("gb", status_);
    heap_caps_free(fb_);
    fb_ = nullptr;
    return false;
  }

  // NULL video callback: gnuboy fills the framebuffer during gnuboy_run(true)
  // and the caller blits afterwards. NULL audio callback: silent v1.
  if (gnuboy_init(GB_SAMPLERATE, GB_AUDIO_MONO_S16, GB_PIXEL_565_LE, nullptr, nullptr) != 0) {
    status_ = "gnuboy_init failed";
    Logger::error("gb", status_);
    return false;
  }
  gnuboy_set_framebuffer(fb_);
  gnuboy_set_soundbuffer(soundScratch_, kSoundScratchSamples);

  ready_ = true;
  status_ = "gnuboy ready (silent)";
  Logger::info("gb", status_);
  return true;
}

bool GameBoyHost::loadRom(const String &romPath, const String &savePath) {
  if (!ready_ && !begin()) return false;

  if (romLoaded_) {
    // Flush the outgoing cartridge before swapping games.
    if (pendingSave_) save();
    gnuboy_free_rom();
    romLoaded_ = false;
  }

  if (romPath.length() == 0) {
    status_ = "no ROM path";
    Logger::error("gb", status_);
    return false;
  }

  // gnuboy does its own stdio file I/O, so romPath must be reachable through
  // the FAT VFS (i.e. the SD card mounted at GB_SD_ROOT).
  if (gnuboy_load_rom_file(romPath.c_str()) != 0) {
    status_ = String("ROM load failed: ") + romPath;
    Logger::error("gb", status_);
    return false;
  }

  savePath_ = savePath;
  gnuboy_reset(true);
  // Battery SRAM is optional - a brand new game simply has no .sav yet.
  if (savePath_.length() > 0) {
    gnuboy_load_sram(savePath_.c_str());
  }

  romLoaded_ = true;
  pendingSave_ = false;
  frames_ = 0;
  status_ = String("loaded ") + romPath;
  Logger::info("gb", status_);
  return true;
}

void GameBoyHost::runFrame(bool draw) {
  if (!romLoaded_) return;
  gnuboy_run(draw);
  frames_++;
  if (gnuboy_sram_dirty()) {
    pendingSave_ = true;
    lastDirtyMs_ = millis();
  }
}

void GameBoyHost::setPad(uint32_t buttons) {
  if (!ready_) return;
  gnuboy_set_pad((int)buttons);
}

bool GameBoyHost::sramDirty() const { return pendingSave_; }

bool GameBoyHost::save() {
  if (!romLoaded_ || savePath_.length() == 0) return false;
  bool ok = (gnuboy_save_sram(savePath_.c_str(), false) == 0);
  if (ok) {
    pendingSave_ = false;
    Logger::info("gb", String("battery save written: ") + savePath_);
  } else {
    Logger::error("gb", String("battery save FAILED: ") + savePath_);
  }
  return ok;
}

void GameBoyHost::tickSave(uint32_t nowMs) {
  if (!pendingSave_) return;
  if ((uint32_t)(nowMs - lastDirtyMs_) < kSaveDebounceMs) return;
  save();
}
