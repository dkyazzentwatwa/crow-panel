#include "KeyAudio.h"

namespace {
// Named in every build, so a silent build can still say what it would play.
// Order must match KeyAudio::Profile.
const char *const kProfileNames[] = {"Off", "Blue", "Brown", "Red"};
}  // namespace

#if USE_CYPHER_KEYS_AUDIO && __has_include(<driver/i2s_std.h>)

#include <Preferences.h>
#include <driver/i2s_std.h>
#include <math.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

#include <CrowPanelShared.h>  // HardwareProfile definition (audio pins)

namespace {

constexpr uint32_t kRate = CYPHER_KEYS_AUDIO_SAMPLE_RATE;
// I2S DMA geometry copied from project 09's proven engine: one render block per
// DMA descriptor, so queued audio is DESC * BLOCK frames (4 * 128 @ 22050 Hz =
// ~23 ms). Small enough that the click still lands with the finger, large enough
// that a busy display frame cannot starve the DMA into an audible dropout.
constexpr uint32_t kBlockFrames = 128;
constexpr uint32_t kDmaDesc = 4;
constexpr uint32_t kRingMs = kBlockFrames * kDmaDesc * 1000 / kRate;
constexpr uint32_t kTaskStack = 4096;
constexpr uint32_t kTaskPrio = 10;
constexpr uint32_t kTaskCore = 0;  // keep core 1 free for HID + display

// Must match KeyAudio::kVariants (private, so it cannot be named here).
constexpr uint8_t kVariantCount = 3;

constexpr float kTwoPi = 6.28318530718f;
// ~0.61 FS per clip peak. Voices are clamped rather than scaled, so this is the
// headroom that keeps two overlapping full-level clicks clean.
constexpr float kPeak = 20000.0f;

// Mix buffers. One instance, one render task, so file-scope internal SRAM is
// simpler than class members - and costs nothing at all in silent builds.
int32_t sAcc[kBlockFrames];
int16_t sOut[kBlockFrames * 2];

// One switch sound: three exponentially decaying layers summed.
//   noise - the plastic/metal transient. One-pole lowpassed; lpAlpha is the
//           brightness knob (0.85 ~ raw hiss, 0.12 ~ a muffled bump).
//   tone  - the switch's resonant click pitch.
//   body  - the low "bottoming out" thud.
struct ClickSpec {
  float durMs;
  float toneHz, toneTauMs, toneLevel;
  float noiseLevel, noiseTauMs, lpAlpha;
  float bodyHz, bodyTauMs, bodyLevel;
  float peak;  // overall level, 0..1, relative to kPeak
};

// [profile - 1][0 = press, 1 = release]
const ClickSpec kSpecs[3][2] = {
    // Blue: bright, sharp, loudest, with a real second click on the way up.
    {{24.0f, 2600.0f, 3.5f, 0.72f, 0.85f, 1.1f, 0.85f, 190.0f, 9.0f, 0.32f, 0.97f},
     {18.0f, 1900.0f, 3.0f, 0.70f, 0.60f, 1.0f, 0.80f, 220.0f, 5.0f, 0.14f, 0.70f}},
    // Brown: tactile bump - lower, heavily damped, little noise, quiet release.
    {{26.0f, 1200.0f, 6.0f, 0.60f, 0.30f, 2.2f, 0.30f, 150.0f, 13.0f, 0.45f, 0.62f},
     {14.0f, 1400.0f, 3.0f, 0.26f, 0.12f, 1.4f, 0.28f, 175.0f, 6.0f, 0.12f, 0.28f}},
    // Red: linear - almost all low thud, very quiet, release barely there.
    {{28.0f, 520.0f, 6.5f, 0.20f, 0.10f, 2.5f, 0.14f, 110.0f, 16.0f, 0.85f, 0.38f},
     {12.0f, 300.0f, 4.0f, 0.08f, 0.04f, 1.5f, 0.12f, 95.0f, 7.0f, 0.30f, 0.13f}},
};

// Per-variant jitter. Deterministic, so the set is identical every boot.
const float kPitchJit[kVariantCount] = {0.94f, 1.00f, 1.07f};
const float kLevelJit[kVariantCount] = {0.90f, 1.00f, 0.95f};
const float kTauJit[kVariantCount] = {0.93f, 1.00f, 1.08f};

float clamp1(float v) {
  if (v > 1.0f) return 1.0f;
  if (v < -1.0f) return -1.0f;
  return v;
}

int16_t *allocPcm(uint32_t frames) {
  size_t bytes = (size_t)frames * sizeof(int16_t);
#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
  int16_t *pcm = (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (pcm != nullptr) {
    return pcm;
  }
#endif
  return (int16_t *)malloc(bytes);
}

// Renders one clip. Pure float math, run once at boot; the render task only ever
// reads the int16 result, which is kept for the life of the sketch.
int16_t *renderClick(const ClickSpec &spec, uint8_t variant, uint8_t seed,
                     uint32_t *framesOut) {
  const float pitch = kPitchJit[variant];
  const float level = kLevelJit[variant];
  const float tauMul = kTauJit[variant];

  uint32_t frames = (uint32_t)(spec.durMs * 0.001f * (float)kRate);
  if (frames < 8) frames = 8;
  int16_t *pcm = allocPcm(frames);
  if (pcm == nullptr) {
    return nullptr;
  }

  const float toneTau = spec.toneTauMs * 0.001f * tauMul;
  const float noiseTau = spec.noiseTauMs * 0.001f * tauMul;
  const float bodyTau = spec.bodyTauMs * 0.001f * tauMul;
  // Linear tail so a clip whose envelope has not fully decayed still ends at
  // zero; a truncated buffer would add its own click on every playback.
  const uint32_t fadeFrames = frames / 6 + 1;

  uint32_t noiseState = 0x1234ABCDu + (uint32_t)(seed + 1) * 0x9E3779B9u;
  float lp = 0.0f;

  for (uint32_t n = 0; n < frames; ++n) {
    const float t = (float)n / (float)kRate;

    // xorshift32 noise in [-1, 1], then a one-pole lowpass for timbre.
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    const float raw = (float)(int32_t)noiseState / 2147483648.0f;
    lp += spec.lpAlpha * (raw - lp);

    float value = spec.noiseLevel * lp * expf(-t / noiseTau) +
                  spec.toneLevel * sinf(kTwoPi * spec.toneHz * pitch * t) *
                      expf(-t / toneTau) +
                  spec.bodyLevel * sinf(kTwoPi * spec.bodyHz * pitch * t) *
                      expf(-t / bodyTau);
    value *= spec.peak * level;
    if (n + fadeFrames >= frames) {
      value *= (float)(frames - n) / (float)fadeFrames;
    }
    pcm[n] = (int16_t)(clamp1(value) * kPeak);
  }

  *framesOut = frames;
  return pcm;
}

}  // namespace

bool KeyAudio::begin(const HardwareProfile &profile, Print &log) {
  loadPrefs_();
  ampPin_ = profile.audio.control;
  ampActiveHigh_ = profile.audio.controlActiveHigh;
  // Park the amp in its sleep state (level from the profile - on this board the
  // control is active-LOW, never hardcode it) before the bus exists, so a failed
  // bring-up below leaves a quiet speaker rather than a floating enable pin.
  pinMode(ampPin_, OUTPUT);
  setAmp_(false);

  i2s_chan_config_t chanCfg = {};
  chanCfg.id = I2S_NUM_AUTO;
  chanCfg.role = I2S_ROLE_MASTER;
  chanCfg.dma_desc_num = kDmaDesc;
  chanCfg.dma_frame_num = kBlockFrames;
  chanCfg.auto_clear = true;
  i2s_chan_handle_t tx = nullptr;
  if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) {
    log.println(F("[keyaudio] i2s_new_channel failed"));
    return false;
  }

  // Philips slot, 16-bit stereo, no MCLK: the NS4168 derives everything from
  // BCLK/LRCLK. Pins come from HardwareProfile.
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
    log.println(F("[keyaudio] i2s std init/enable failed"));
    i2s_del_channel(tx);
    return false;
  }
  txChan_ = tx;

  // Every profile's clips are built even when the stored profile is Off, so a
  // later `sound blue` is instant. The whole set is ~16 KB.
  synthesizeAll_(log);

  // Stream silence before waking the amp: this ordering is what keeps project
  // 09's bring-up free of a startup pop.
  memset(sOut, 0, sizeof(sOut));
  size_t written = 0;
  for (uint8_t i = 0; i < 2; ++i) {
    i2s_channel_write(tx, sOut, sizeof(sOut), &written, portMAX_DELAY);
  }
  setAmp_(profile_ != kOff);

  running_ = true;
  TaskHandle_t task = nullptr;
  if (xTaskCreatePinnedToCore(renderTrampoline_, "keyclick", kTaskStack, this,
                              kTaskPrio, &task, kTaskCore) != pdPASS) {
    running_ = false;
    setAmp_(false);
    log.println(F("[keyaudio] render task create failed"));
    return false;
  }
  task_ = task;
  log.println(String("[keyaudio] i2s up: ") + String(kRate) + "Hz ring " +
              String(kRingMs) + "ms voices " + String((int)kVoices) +
              " profile " + profileName() + " vol " + String((int)volume_) + "%");
  return true;
}

bool KeyAudio::synthesizeAll_(Print &log) {
  uint8_t made = 0;
  uint32_t bytes = 0;
  for (uint8_t p = 0; p < kRealProfiles; ++p) {
    for (uint8_t rel = 0; rel < 2; ++rel) {
      for (uint8_t v = 0; v < kVariants; ++v) {
        const uint8_t idx = (uint8_t)((p * 2 + rel) * kVariants + v);
        uint32_t frames = 0;
        int16_t *pcm = renderClick(kSpecs[p][rel], v, idx, &frames);
        clips_[idx].pcm = pcm;
        clips_[idx].frames = (pcm != nullptr) ? frames : 0;
        if (pcm != nullptr) {
          ++made;
          bytes += frames * sizeof(int16_t);
        }
      }
    }
  }
  log.println(String("[keyaudio] synthesized ") + String((int)made) + "/" +
              String((int)kClipCount) + " clips, " + String(bytes) + " B");
  return made == kClipCount;
}

void KeyAudio::setAmp_(bool on) {
  digitalWrite(ampPin_, on == ampActiveHigh_ ? HIGH : LOW);
  ampOn_ = on;
}

// Loop context. Resolves which clip to play and stamps one byte - no mixing, no
// float math, no blocking.
void KeyAudio::trigger_(bool releaseSound, KeyClass k, uint8_t row) {
  if (!running_) return;

  // A loaded SD pack wins. The slot is resolved HERE, in loop context, against
  // the live set's present-mask, so the render task only ever dereferences a
  // slot it was handed. The fallback order (class clip -> GENERIC_R<row> ->
  // GENERIC_R0 for a press, class clip -> GENERIC for a release) lives in
  // KeySoundPacks so the host tests can prove it.
  if (packSelected_ && livePack_ < 2) {
    const uint16_t present = packs_[livePack_].present;
    const uint8_t slot =
        releaseSound ? KeySoundPacks::resolveReleaseSlot(present, (uint8_t)k)
                     : KeySoundPacks::resolvePressSlot(present, (uint8_t)k, row);
    if (slot != KeySoundPacks::kSlotNone) {
      ++packHits_;
      push_((uint8_t)(kPackFlag | slot));
      return;
    }
    // The pack has no clip for this event (mxblue has no release clips at all),
    // so fall through to the synthesized profile - which is silent if it is Off.
  }

  const Profile p = profile_;
  if (p == kOff || (uint8_t)p >= kProfileCount) return;

  // Rotate variants with a one-bit jitter so a fast typist never hears the same
  // waveform twice running. xorshift32 is three shifts: cheap on the touch path.
  jitter_ ^= jitter_ << 13;
  jitter_ ^= jitter_ >> 17;
  jitter_ ^= jitter_ << 5;
  const uint8_t v = (uint8_t)((nextVariant_ + 1 + (jitter_ & 1u)) % kVariants);
  nextVariant_ = v;

  const uint8_t idx = (uint8_t)(
      (((uint8_t)p - 1) * 2 + (releaseSound ? 1 : 0)) * kVariants + v);
  if (idx >= kClipCount || clips_[idx].pcm == nullptr) return;
  push_(idx);
}

void KeyAudio::push_(uint8_t tag) {
  const uint8_t head = trigHead_;
  const uint8_t next = (uint8_t)((head + 1) & (kTrigRing - 1));
  if (next == trigTail_) {
    ++dropped_;  // ring full: drop rather than stall the touch path
    return;
  }
  trigRing_[head] = tag;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  trigHead_ = next;
  ++triggered_;
}

void KeyAudio::press(KeyClass k, uint8_t row) { trigger_(false, k, row); }
void KeyAudio::release(KeyClass k, uint8_t row) { trigger_(true, k, row); }

void KeyAudio::drainTriggers_() {
  while (trigTail_ != trigHead_) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint8_t tag = trigRing_[trigTail_];
    trigTail_ = (uint8_t)((trigTail_ + 1) & (kTrigRing - 1));

    const int16_t *pcm = nullptr;
    uint32_t frames = 0;
    if ((tag & kPackFlag) != 0) {
      const uint8_t slot = (uint8_t)(tag & (uint8_t)(kPackFlag - 1));
      const uint8_t set = activePack_;
      if (set < 2 && slot < KeySoundPacks::kSlotCount) {
        pcm = packs_[set].slots[slot].pcm;
        frames = packs_[set].slots[slot].frames;
      }
    } else if (tag < kClipCount) {
      pcm = clips_[tag].pcm;
      frames = clips_[tag].frames;
    }
    if (pcm == nullptr || frames == 0) continue;

    // Rolling voice-steal: take a free slot, else the next one in rotation.
    Voice *voice = nullptr;
    for (uint8_t i = 0; i < kVoices; ++i) {
      if (!voices_[i].active) {
        voice = &voices_[i];
        break;
      }
    }
    if (voice == nullptr) {
      voice = &voices_[voiceCursor_];
      voiceCursor_ = (uint8_t)((voiceCursor_ + 1) % kVoices);
    }

    uint8_t vol = volume_;
    if (vol > 100) vol = 100;
    voice->pcm = pcm;
    voice->frames = frames;
    voice->pos = 0;
    voice->gainQ12 = (int32_t)(((uint32_t)vol * 4096u) / 100u);
    voice->active = true;
  }
}

// Render-task side of the clip-set swap. Every voice is killed and every pending
// trigger dropped BEFORE the active set changes, which is exactly what makes it
// safe for loop context to free the retired buffers once the ack lands: after
// this returns, nothing in the mixer can still be reading the old set.
void KeyAudio::servicePackSwap_() {
  const uint8_t ticket = swapRequest_;
  if (ticket == swapAck_) return;
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  for (uint8_t i = 0; i < kVoices; ++i) voices_[i].active = false;
  trigTail_ = trigHead_;
  activePack_ = swapTo_;
  __atomic_thread_fence(__ATOMIC_RELEASE);
  swapAck_ = ticket;
}

KeySoundPacks::ClipSet *KeyAudio::beginPackStaging() {
  if (running_) {
    // Settle any swap the render task has not acknowledged yet. Until it does,
    // activePack_ is stale, so we cannot tell which set the mixer is really on -
    // and stagingIndex_() could name the live one.
    const uint32_t start = millis();
    while (swapAck_ != swapRequest_ && (millis() - start) < kSwapWaitMs) delay(1);
    if (swapAck_ != swapRequest_) return nullptr;  // render task wedged
    // Once acked, activePack_ == livePack_, so stagingIndex_() is provably the
    // set nothing is reading. This is also where a swap that timed out earlier
    // (and therefore left its predecessor allocated) gets reclaimed.
  }
  KeySoundPacks::ClipSet &set = packs_[stagingIndex_()];
  set.freeAll();
  return &set;
}

void KeyAudio::discardPackStaging() { packs_[stagingIndex_()].freeAll(); }

bool KeyAudio::commitPackStaging(const char *name, String &status) {
  const uint8_t staged = stagingIndex_();
  KeySoundPacks::ClipSet &set = packs_[staged];
  if (!set.has(KeySoundPacks::kPressR0)) {
    set.freeAll();
    status += "  (nothing playable staged)";
    return false;
  }
  set.setName(name);
  const uint8_t retire = livePack_;
  bool acked = true;

  if (running_) {
    swapTo_ = staged;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    uint8_t ticket = (uint8_t)(swapRequest_ + 1);
    if (ticket == swapAck_) ++ticket;  // never publish an already-acked ticket
    swapRequest_ = ticket;
    const uint32_t start = millis();
    while (swapAck_ != ticket && (millis() - start) < kSwapWaitMs) delay(1);
    acked = swapAck_ == ticket;
  } else {
    activePack_ = staged;  // no render task exists, so nothing can be reading
  }

  livePack_ = staged;
  packSelected_ = true;
  // Keep the fallback audible: a pack that is missing a clip (or a card pulled
  // at the next boot) drops back to the synthesized profile, and Off would be
  // indistinguishable from a broken load.
  if (profile_ == kOff) profile_ = kBlue;
  if (acked) {
    if (retire < 2) packs_[retire].freeAll();
  } else {
    // The render task never answered - it is the only thing that can. Adopt the
    // pack, but do NOT free the retired buffers: the mixer might still be reading
    // them. beginPackStaging() reclaims them once the ack finally lands, and
    // refuses to stage anything until it does.
    status += "  (swap not acked in " + String(kSwapWaitMs) +
              "ms; previous pack still allocated)";
  }
  setAmp_(running_);
  persistPrefs_();
  return true;
}

const char *KeyAudio::packName() const {
  return livePack_ < 2 ? packs_[livePack_].name : "";
}

const char *KeyAudio::soundName() const {
  return packActive() ? packs_[livePack_].name : profileName();
}

void KeyAudio::renderTrampoline_(void *self) {
  static_cast<KeyAudio *>(self)->renderTask_();
}

void KeyAudio::renderTask_() {
  i2s_chan_handle_t tx = (i2s_chan_handle_t)txChan_;
  for (;;) {
    servicePackSwap_();  // before draining: a swap voids the pending triggers
    drainTriggers_();

    memset(sAcc, 0, sizeof(sAcc));
    for (uint8_t i = 0; i < kVoices; ++i) {
      Voice &voice = voices_[i];
      if (!voice.active) continue;
      const int16_t *pcm = voice.pcm;
      uint32_t n = 0;
      while (n < kBlockFrames && voice.pos < voice.frames) {
        sAcc[n] += ((int32_t)pcm[voice.pos] * voice.gainQ12) >> 12;
        ++voice.pos;
        ++n;
      }
      if (voice.pos >= voice.frames) voice.active = false;
    }

    // Clamp rather than scale the sum: overlapping clicks are transients, so
    // hard limiting is inaudible where an automatic gain dip would pump.
    for (uint32_t n = 0; n < kBlockFrames; ++n) {
      int32_t s = sAcc[n];
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      sOut[2 * n] = (int16_t)s;
      sOut[2 * n + 1] = (int16_t)s;
    }

    // The blocking write paces this task; the silence between clicks keeps the
    // DMA fed, which is also what keeps the amp quiet while idle.
    size_t written = 0;
    i2s_channel_write(tx, sOut, sizeof(sOut), &written, portMAX_DELAY);
  }
}

void KeyAudio::setProfile(Profile p) {
  if ((uint8_t)p >= kProfileCount) return;
  profile_ = p;
  // Picking a synthesized profile deselects the pack but keeps it RESIDENT, so
  // switching back to it is instant and costs no SD read.
  packSelected_ = false;
  persistPrefs_();
  setAmp_(running_ && p != kOff);
}

void KeyAudio::setVolume(uint8_t pct) {
  volume_ = pct > 100 ? 100 : pct;
  persistPrefs_();
}

bool KeyAudio::selectProfileByName(const String &name) {
  String want = name;
  want.trim();
  want.toLowerCase();
  if (want.length() == 0) return false;
  for (uint8_t i = 0; i < kProfileCount; ++i) {
    String have = kProfileNames[i];
    have.toLowerCase();
    if (have.startsWith(want)) {
      setProfile((Profile)i);
      return true;
    }
  }
  return false;
}

const char *KeyAudio::profileName() const {
  const uint8_t p = (uint8_t)profile_;
  return kProfileNames[p < kProfileCount ? p : 0];
}

String KeyAudio::status() const {
  String out = String("sound: ") + soundName();
  if (packActive()) {
    out += " (SD pack, " + String((uint32_t)packs_[livePack_].clips) +
           " clips, " + String(packs_[livePack_].bytes / 1024) + "KB, fallback " +
           profileName() + ")";
  }
  out += "  vol " + String((int)volume_) + "%";
  if (!running_) {
    return out + "  (i2s down - bring-up failed, nothing is audible)";
  }
  out += "  amp ";
  out += ampOn_ ? "on" : "asleep";
  out += "  i2s " + String(kRate) + "Hz ring " + String(kRingMs) + "ms";
  out += "  clicks " + String(triggered_);
  if (packHits_ != 0) out += " (" + String(packHits_) + " sampled)";
  if (dropped_ != 0) out += " dropped " + String(dropped_);
  if (packStatus_[0] != '\0') out += String("  [") + packStatus_ + "]";
  return out;
}

// Same NVS namespace the preset and the theme use; keys "snd", "sndvol" and
// "sndpack" (the pack folder name, so the choice survives a reboot by NAME - a
// card can be re-imaged and the index of a pack is not stable).
void KeyAudio::loadPrefs_() {
  Preferences prefs;
  if (!prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, true)) return;
  const uint32_t p = prefs.getUInt("snd", (uint32_t)profile_);
  const uint32_t v = prefs.getUInt("sndvol", (uint32_t)volume_);
  prefs.getString("sndpack", storedPack_, sizeof(storedPack_));
  prefs.end();
  if (p < kProfileCount) profile_ = (Profile)p;
  if (v <= 100) volume_ = (uint8_t)v;
}

void KeyAudio::persistPrefs_() const {
  Preferences prefs;
  if (!prefs.begin(CYPHER_KEYS_NVS_NAMESPACE, false)) return;
  prefs.putUInt("snd", (uint32_t)profile_);
  prefs.putUInt("sndvol", (uint32_t)volume_);
  prefs.putString("sndpack", packActive() ? packs_[livePack_].name : "");
  prefs.end();
}

#else  // silent stub: USE_CYPHER_KEYS_AUDIO=0 or no IDF i2s_std header

bool KeyAudio::begin(const HardwareProfile &, Print &log) {
  log.println(F("[keyaudio] silent build"));
  return false;
}

void KeyAudio::press(KeyClass, uint8_t) {}
void KeyAudio::release(KeyClass, uint8_t) {}
void KeyAudio::setProfile(Profile) {}
void KeyAudio::setVolume(uint8_t) {}
bool KeyAudio::selectProfileByName(const String &) { return false; }

// Sound packs need the engine, so in a silent build the staging set exists (the
// loader still compiles) but nothing is ever adopted. No render task exists, so
// there is nothing to synchronize with.
KeySoundPacks::ClipSet *KeyAudio::beginPackStaging() {
  packs_[0].freeAll();
  return &packs_[0];
}

void KeyAudio::discardPackStaging() { packs_[0].freeAll(); }

bool KeyAudio::commitPackStaging(const char *, String &status) {
  packs_[0].freeAll();
  status += "  (silent build: USE_CYPHER_KEYS_AUDIO=0, nothing adopted)";
  return false;
}

const char *KeyAudio::packName() const { return ""; }
const char *KeyAudio::soundName() const { return profileName(); }

// Honest in a silent build: nothing can be played, so nothing is selected.
const char *KeyAudio::profileName() const { return kProfileNames[kOff]; }

String KeyAudio::status() const {
  return String(F("sound: silent (USE_CYPHER_KEYS_AUDIO=0)"));
}

#endif
