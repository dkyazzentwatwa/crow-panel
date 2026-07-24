#include "GameBoyHost.h"

#include "GbAudio.h"
#include <CrowPanelShared.h>
#include "esp_heap_caps.h"

extern "C" {
#include "gnuboy/gnuboy.h"
}

// gnuboy takes a plain C function pointer for audio, so route it through a
// file-static instance pointer. Only one GameBoyHost ever exists.
static GbAudio *s_audioSink = nullptr;

static void gb_audio_cb(void *buffer, size_t length) {
  // `length` counts int16 values written by the mixer (stereo frames * 2).
  if (s_audioSink) s_audioSink->submit((const int16_t *)buffer, length);
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

bool GameBoyHost::begin(GbAudio *audio) {
  if (ready_) return true;
  audio_ = audio;
  s_audioSink = audio;

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
  // and the caller blits afterwards.
  // Audio: STEREO_S16 so the mixer emits interleaved stereo that feeds I2S with
  // no conversion. The callback is only wired when there is a sink - with none,
  // gb_sound_emulate() still runs but nothing is ever emitted (silent).
  gb_audio_cb_t *audioCb = (audio != nullptr) ? gb_audio_cb : nullptr;
  if (gnuboy_init(GB_SAMPLERATE, GB_AUDIO_STEREO_S16, GB_PIXEL_565_LE, nullptr, audioCb) != 0) {
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

bool GameBoyHost::start(const String &romPath, const String &savePath) {
  if (!ready_ && !begin(audio_)) return false;

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

void GameBoyHost::stop() {
  if (!romLoaded_) return;
  if (pendingSave_) save();
  gnuboy_free_rom();
  romLoaded_ = false;
  status_ = "stopped";
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

bool GameBoyHost::saveState(const String &path) {
  if (!romLoaded_ || path.length() == 0) return false;
  const bool ok = (gnuboy_save_state(path.c_str()) == 0);
  Logger::info("gb", String(ok ? "state saved: " : "state save FAILED: ") + path);
  return ok;
}

bool GameBoyHost::loadState(const String &path) {
  if (!romLoaded_ || path.length() == 0) return false;
  const bool ok = (gnuboy_load_state(path.c_str()) == 0);
  Logger::info("gb", String(ok ? "state loaded: " : "state load FAILED (missing?): ") + path);
  return ok;
}

bool GameBoyHost::saveThumb(const String &statePath) {
#if USE_GB_SD
  if (!romLoaded_ || !fb_ || statePath.length() == 0) return false;
  // Box-filter the 160x144 frame down to 40x36 by averaging each 4x4 block -
  // cheaper than it sounds (5760 source pixels) and far kinder to the eye than
  // point-sampling, which turns Game Boy text into noise.
  static uint16_t thumb[kThumbW * kThumbH];
  for (int16_t ty = 0; ty < kThumbH; ty++) {
    for (int16_t tx = 0; tx < kThumbW; tx++) {
      uint32_t r = 0, g = 0, b = 0;
      for (uint8_t sy = 0; sy < kThumbScale; sy++) {
        const uint16_t *row = fb_ + (size_t)(ty * kThumbScale + sy) * GB_W + tx * kThumbScale;
        for (uint8_t sx = 0; sx < kThumbScale; sx++) {
          const uint16_t c = row[sx];
          r += (c >> 11) & 0x1F;
          g += (c >> 5) & 0x3F;
          b += c & 0x1F;
        }
      }
      const uint16_t n = kThumbScale * kThumbScale;
      thumb[ty * kThumbW + tx] =
          (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
    }
  }
  FILE *f = fopen(thumbPath(statePath).c_str(), "wb");
  if (!f) return false;
  const size_t want = sizeof(thumb);
  const bool ok = fwrite(thumb, 1, want, f) == want;
  fclose(f);
  return ok;
#else
  (void)statePath;
  return false;
#endif
}

bool GameBoyHost::loadThumb(const String &statePath, uint16_t *out) {
#if USE_GB_SD
  if (!out || statePath.length() == 0) return false;
  FILE *f = fopen(thumbPath(statePath).c_str(), "rb");
  if (!f) return false;
  const size_t want = (size_t)kThumbW * kThumbH * sizeof(uint16_t);
  const bool ok = fread(out, 1, want, f) == want;
  fclose(f);
  return ok;
#else
  (void)statePath;
  (void)out;
  return false;
#endif
}

void GameBoyHost::tickSave(uint32_t nowMs) {
  if (!pendingSave_) return;
  if ((uint32_t)(nowMs - lastDirtyMs_) < kSaveDebounceMs) return;
  save();
}
