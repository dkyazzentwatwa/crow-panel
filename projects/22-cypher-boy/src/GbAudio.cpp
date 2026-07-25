#include "GbAudio.h"

#include <CrowPanelShared.h>
#include <math.h>

#if USE_GB_AUDIO && __has_include(<driver/i2s_std.h>)
#define CYPHER_BOY_HAS_I2S 1
#include <driver/i2s_std.h>
#include "esp_heap_caps.h"
#else
#define CYPHER_BOY_HAS_I2S 0
#endif

namespace {
// One emulated frame at 32 kHz is ~533 stereo frames; this is comfortably more
// so gnuboy never overflows mid-frame.
const size_t kScratchSamples = 2048;  // int16 values (stereo frames * 2)

#if CYPHER_BOY_HAS_I2S
// 4 x 256 frames = 1024 frames ~= 32 ms at 32 kHz. Small enough to feel
// responsive, large enough to ride out a slow video frame.
const int kDmaDesc = 4;
const int kDmaFrames = 256;
#endif
}  // namespace

bool GbAudio::begin() {
#if CYPHER_BOY_HAS_I2S
  if (ready_) return true;

  const HardwareProfile &hp = activeHardwareProfile();
  ampPin_ = hp.audio.control;
  ampActiveHigh_ = hp.audio.controlActiveHigh;

  // Park the amp OFF before touching the bus so nothing pops.
  pinMode(ampPin_, OUTPUT);
  ampEnable(false);

  scratch_ = (int16_t *)heap_caps_malloc(kScratchSamples * sizeof(int16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!scratch_) {
    status_ = "audio scratch alloc failed; running silent";
    Logger::error("gbaudio", status_);
    return false;
  }
  scratchSamples_ = kScratchSamples;

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = kDmaDesc;
  chanCfg.dma_frame_num = kDmaFrames;
  chanCfg.auto_clear = true;  // emit silence on underrun instead of stale audio

  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
    status_ = "i2s_new_channel failed; running silent";
    Logger::error("gbaudio", status_);
    return false;
  }

  i2s_std_config_t stdCfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)GB_SAMPLERATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,  // NS4168 needs no MCLK
          .bclk = (gpio_num_t)hp.audio.bclk,
          .ws = (gpio_num_t)hp.audio.lrclk,
          .dout = (gpio_num_t)hp.audio.sdata,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {false, false, false},
      },
  };

  if (i2s_channel_init_std_mode(tx, &stdCfg) != ESP_OK ||
      i2s_channel_enable(tx) != ESP_OK) {
    status_ = "i2s init/enable failed; running silent";
    Logger::error("gbaudio", status_);
    i2s_del_channel(tx);
    return false;
  }
  txChan_ = tx;

  // Stream a little silence so the amp wakes onto a clean bus (project 09's
  // hard-won lesson - without this you get an audible pop on enable).
  memset(scratch_, 0, scratchSamples_ * sizeof(int16_t));
  for (int i = 0; i < 4; i++) {
    size_t written = 0;
    i2s_channel_write(tx, scratch_, scratchSamples_ * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(50));
  }
  ampEnable(true);

  ready_ = true;
  rate_ = GB_SAMPLERATE;
  status_ = String("i2s ready @") + GB_SAMPLERATE + "Hz, amp IO" + ampPin_ +
            (ampActiveHigh_ ? " (active-HIGH)" : " (active-LOW)");
  Logger::info("gbaudio", status_);
  return true;
#else
  status_ = "USE_GB_AUDIO=0; silent";
  return false;
#endif
}

void GbAudio::ampEnable(bool on) {
#if CYPHER_BOY_HAS_I2S
  // Polarity from HardwareProfile. On this panel IO30 is ACTIVE-LOW: driving
  // it HIGH disables the speaker path entirely.
  digitalWrite(ampPin_, on == ampActiveHigh_ ? HIGH : LOW);
#else
  (void)on;
#endif
}

void GbAudio::shutdown() {
#if CYPHER_BOY_HAS_I2S
  if (!ready_) return;
  ampEnable(false);
  if (txChan_) {
    i2s_channel_disable((i2s_chan_handle_t)txChan_);
    i2s_del_channel((i2s_chan_handle_t)txChan_);
    txChan_ = nullptr;
  }
  ready_ = false;
#endif
}

bool GbAudio::setSampleRate(uint32_t hz) {
#if CYPHER_BOY_HAS_I2S
  if (!ready_ || hz == rate_ || hz == 0) return ready_;
  // Reconfiguring the clock needs the channel disabled. Genesis is ~53 kHz and
  // Game Boy 32 kHz; retuning the hardware beats resampling in software.
  i2s_chan_handle_t tx = (i2s_chan_handle_t)txChan_;
  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
  if (i2s_channel_disable(tx) != ESP_OK) return false;
  const bool ok = i2s_channel_reconfig_std_clock(tx, &clk) == ESP_OK;
  if (i2s_channel_enable(tx) != ESP_OK) return false;
  if (ok) {
    rate_ = hz;
    Logger::info("gbaudio", String("sample rate -> ") + hz + " Hz");
  }
  return ok;
#else
  (void)hz;
  return false;
#endif
}

void GbAudio::setVolume(uint8_t v) { volume_ = v; }

void GbAudio::setMuted(bool m) {
  muted_ = m;
#if CYPHER_BOY_HAS_I2S
  if (ready_) ampEnable(!m);
#endif
}

namespace {
#if CYPHER_BOY_HAS_I2S
// Fill `buf` with `frames` stereo frames of a decaying sine at `hz`, phase
// carried across calls so chunk boundaries do not click.
void fillTone(int16_t *buf, size_t frames, float hz, float &phase, float amp,
              float decayPerFrame) {
  const float step = 2.0f * (float)M_PI * hz / (float)GB_SAMPLERATE;
  for (size_t i = 0; i < frames; i++) {
    const int16_t v = (int16_t)(sinf(phase) * amp * 12000.0f);
    buf[i * 2] = v;
    buf[i * 2 + 1] = v;
    phase += step;
    if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    amp *= decayPerFrame;
  }
}
#endif
}  // namespace

void GbAudio::playChime() {
#if CYPHER_BOY_HAS_I2S
  if (!ready_ || muted_) return;
  // Two rising notes with an exponential tail - the shape of a console power-on
  // without borrowing anyone's actual startup jingle.
  const float notes[2] = {587.33f, 880.0f};  // D5 -> A5
  const size_t chunkFrames = 256;
  int16_t chunk[chunkFrames * 2];
  for (int n = 0; n < 2; n++) {
    float phase = 0.0f;
    float amp = 1.0f;
    const int chunks = (n == 0) ? 5 : 12;  // short, then a longer ring-out
    for (int c = 0; c < chunks; c++) {
      fillTone(chunk, chunkFrames, notes[n], phase, amp, 0.99985f);
      amp *= 0.86f;
      submit(chunk, chunkFrames * 2);
    }
  }
#endif
}

void GbAudio::playClick() {
#if CYPHER_BOY_HAS_I2S
  if (!ready_ || muted_) return;
  const size_t frames = 128;  // ~4 ms at 32 kHz
  int16_t chunk[frames * 2];
  float phase = 0.0f;
  fillTone(chunk, frames, 1800.0f, phase, 0.35f, 0.972f);
  submit(chunk, frames * 2);
#endif
}

void GbAudio::submit(const int16_t *data, size_t samples) {
#if CYPHER_BOY_HAS_I2S
  if (!ready_ || muted_ || !data || samples == 0) return;
  if (samples > scratchSamples_) samples = scratchSamples_;

  // gnuboy has no volume control of its own, so scale here. >>8 keeps it to a
  // shift rather than a divide in the per-sample loop.
  if (volume_ >= 255) {
    memcpy(scratch_, data, samples * sizeof(int16_t));
  } else {
    const int32_t vol = volume_;
    for (size_t i = 0; i < samples; i++) {
      scratch_[i] = (int16_t)(((int32_t)data[i] * vol) >> 8);
    }
  }

  // BOUNDED wait: this paces the emulator to real time when audio is the
  // bottleneck, but can never hang the app if I2S stalls - we drop the block
  // and count it instead.
  size_t written = 0;
  esp_err_t err = i2s_channel_write((i2s_chan_handle_t)txChan_, scratch_,
                                    samples * sizeof(int16_t), &written,
                                    pdMS_TO_TICKS(50));
  if (err != ESP_OK || written < samples * sizeof(int16_t)) {
    underruns_++;
  }
#else
  (void)data;
  (void)samples;
#endif
}
