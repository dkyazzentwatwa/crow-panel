// Synthesized confirmation cues through the onboard NS4168 amp.
// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT HARDWARE-VERIFIED -
// no sound produced here has been heard.
//
// The I2S setup is transcribed from project 09's AudioEngine, which is
// field-proven on this board. Two details there are load-bearing and are
// repeated here rather than rediscovered:
//
//  * The amp enable is ACTIVE LOW on this hardware. It comes from
//    profile.audio.controlActiveHigh, never hardcoded - driving IO30 high mutes
//    the speaker while I2S happily keeps streaming, which presents as "the code
//    works but there is no sound".
//  * Silence must be streamed BEFORE raising the enable, or the amp wakes onto
//    an undefined bus and pops.

#include "CamAudio.h"

#if USE_CAM_AUDIO && __has_include(<driver/i2s_std.h>)

#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t kRate = 22050;
constexpr uint16_t kBlockFrames = 128;
// Longest cue, which sizes the render scratch. 200 ms is comfortably above the
// ~120 ms shutter.
constexpr uint16_t kMaxFrames = kRate / 5;

int16_t gBlock[kBlockFrames * 2];  // stereo interleaved

// Deterministic noise. rand() would pull in libc state and vary run to run; a
// tiny xorshift gives the same click every time, which is what you want from a
// shutter sound.
uint32_t gNoiseState = 0x13579BDF;
inline int16_t noise() {
  gNoiseState ^= gNoiseState << 13;
  gNoiseState ^= gNoiseState >> 17;
  gNoiseState ^= gNoiseState << 5;
  return (int16_t)((gNoiseState >> 16) & 0xFFFF);
}

// One-pole low-pass. A raw noise burst is a hiss; rolling the top off turns it
// into something that reads as mechanical rather than digital.
struct LowPass {
  int32_t z = 0;
  inline int16_t operator()(int16_t x, int16_t k) {
    z += ((int32_t)x - z) * k >> 8;
    return (int16_t)z;
  }
};

}  // namespace

bool CamAudio::begin(const HardwareProfile &profile) {
  if (ready_) return true;

  i2s_chan_config_t chanCfg = {};
  chanCfg.id = I2S_NUM_AUTO;
  chanCfg.role = I2S_ROLE_MASTER;
  chanCfg.dma_desc_num = 4;
  chanCfg.dma_frame_num = kBlockFrames;
  chanCfg.auto_clear = true;
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
    lastError_ = "i2s_new_channel failed";
    Logger::warn("audio", lastError_);
    return false;
  }

  // Philips slot, 16-bit stereo, no MCLK - the NS4168 derives its clocks from
  // BCLK/LRCLK and needs no codec initialisation at all.
  i2s_std_config_t stdCfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),
      .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = (gpio_num_t)I2S_GPIO_UNUSED,
              .bclk = (gpio_num_t)profile.audio.bclk,
              .ws = (gpio_num_t)profile.audio.lrclk,
              .dout = (gpio_num_t)profile.audio.sdata,
              .din = (gpio_num_t)I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
          },
  };
  if (i2s_channel_init_std_mode(tx, &stdCfg) != ESP_OK ||
      i2s_channel_enable(tx) != ESP_OK) {
    lastError_ = "i2s std init/enable failed";
    Logger::warn("audio", lastError_);
    i2s_del_channel(tx);
    return false;
  }
  txChan_ = tx;

  // Silence first, THEN the amp enable, or it wakes onto an undefined bus and
  // pops audibly.
  memset(gBlock, 0, sizeof(gBlock));
  size_t written = 0;
  for (uint8_t i = 0; i < 2; i++) {
    i2s_channel_write(tx, gBlock, sizeof(gBlock), &written, portMAX_DELAY);
  }
  pinMode(profile.audio.control, OUTPUT);
  // NEVER hardcode HIGH here - see the file header.
  digitalWrite(profile.audio.control, profile.audio.controlActiveHigh ? HIGH : LOW);

  queue_ = xQueueCreate(4, sizeof(CamSound));
  if (queue_ == nullptr) {
    lastError_ = "could not create the cue queue";
    return false;
  }

  // Core 0: the render loop and camera live on core 1, and a cue must never
  // compete with a frame.
  if (xTaskCreatePinnedToCore(taskTrampoline_, "cam-audio", 4096, this, 2, nullptr, 0) !=
      pdPASS) {
    lastError_ = "could not start the audio task";
    Logger::warn("audio", lastError_);
    return false;
  }

  ready_ = true;
  lastError_ = "";
  Logger::info("audio", String("ready at ") + String(kRate) + " Hz, amp enable IO" +
                            String(profile.audio.control) + " active " +
                            (profile.audio.controlActiveHigh ? "high" : "LOW"));
  return true;
}

void CamAudio::play(CamSound sound) {
  if (!ready_ || !enabled_ || queue_ == nullptr) return;
  // Non-blocking send with no wait: if four cues are already queued, the fifth
  // is dropped. A backlog of shutter clicks is worse than a missing one, and
  // this must never stall the caller.
  xQueueSend((QueueHandle_t)queue_, &sound, 0);
}

void CamAudio::taskTrampoline_(void *self) {
  static_cast<CamAudio *>(self)->taskLoop_();
}

void CamAudio::taskLoop_() {
  CamSound sound;
  for (;;) {
    if (xQueueReceive((QueueHandle_t)queue_, &sound, portMAX_DELAY) == pdTRUE) {
      render_(sound);
    }
  }
}

// Synthesizes and streams one cue. Runs on the audio task, so blocking on the
// I2S write here is correct and costs the render loop nothing.
void CamAudio::render_(CamSound sound) {
  i2s_chan_handle_t tx = (i2s_chan_handle_t)txChan_;
  if (tx == nullptr) return;

  LowPass lp;
  uint32_t frame = 0;
  uint32_t total = 0;

  // Each cue is described by a few numbers rather than a switch full of
  // hand-written loops, so adding one later is a line rather than a function.
  uint32_t toneHz = 0;      // 0 = noise-based
  uint32_t toneHz2 = 0;     // second half, for two-note cues
  int16_t filterK = 200;
  uint32_t decayShift = 6;

  switch (sound) {
    case CamSound::Shutter:
      // Filtered noise with a very fast decay: a mechanical snap.
      total = kRate / 12;  // ~85 ms
      filterK = 150;
      decayShift = 5;
      break;
    case CamSound::RecordStart:
      // Rising two-tone. "Something has begun."
      total = kRate / 8;
      toneHz = 660;
      toneHz2 = 990;
      break;
    case CamSound::RecordStop:
      // The same two notes falling, so start and stop are distinguishable
      // without looking at the screen.
      total = kRate / 8;
      toneHz = 990;
      toneHz2 = 660;
      break;
    case CamSound::Error:
      // Low buzz. Deliberately unpleasant and clearly not a success sound.
      total = kRate / 5;
      toneHz = 160;
      toneHz2 = 160;
      filterK = 90;
      break;
  }
  if (total > kMaxFrames) total = kMaxFrames;

  uint32_t phase = 0;
  while (frame < total) {
    const uint16_t n = (uint16_t)min<uint32_t>(kBlockFrames, total - frame);
    for (uint16_t i = 0; i < n; i++) {
      const uint32_t pos = frame + i;

      // Exponential-ish decay, and a short attack ramp so the cue does not
      // begin with a click of its own.
      int32_t env = 32767 - (int32_t)((pos << decayShift) * 32767 / total);
      if (env < 0) env = 0;
      if (pos < 64) env = env * (int32_t)pos / 64;

      int32_t sample;
      if (toneHz == 0) {
        sample = lp(noise(), filterK);
      } else {
        const uint32_t hz = (pos * 2 < total) ? toneHz : toneHz2;
        phase += (hz << 16) / kRate;
        // Triangle rather than sine: no table, no float, and softer than a
        // square at this duration. Scaled near full range - the low-pass below
        // takes a good deal of it back out.
        const uint32_t p = (phase >> 8) & 0xFF;
        const int32_t tri = (p < 128) ? (int32_t)p * 2 - 128 : 384 - (int32_t)p * 2;
        sample = lp((int16_t)(tri * 250), filterK);
      }

      // Envelope then volume. The shift is 15, not 16: env peaks at 32767, so
      // >>16 was quietly halving every cue and capping the speaker at about
      // -6 dB of what it can do. That is most of why the first build was too
      // quiet to hear across a room.
      int32_t out = (sample * env) >> 15;
      out = (out * volume_) / 100;
      if (out > 32767) out = 32767;
      if (out < -32768) out = -32768;
      gBlock[i * 2] = (int16_t)out;
      gBlock[i * 2 + 1] = (int16_t)out;
    }
    size_t written = 0;
    i2s_channel_write(tx, gBlock, (size_t)n * 2 * sizeof(int16_t), &written,
                      portMAX_DELAY);
    frame += n;
  }

  // Trailing silence so the amp is not left holding the last sample.
  memset(gBlock, 0, sizeof(gBlock));
  size_t written = 0;
  i2s_channel_write(tx, gBlock, sizeof(gBlock), &written, portMAX_DELAY);
}

#else  // built without audio, or no IDF i2s_std available

bool CamAudio::begin(const HardwareProfile &) {
  lastError_ = "built without USE_CAM_AUDIO";
  return false;
}
void CamAudio::play(CamSound) {}

#endif
