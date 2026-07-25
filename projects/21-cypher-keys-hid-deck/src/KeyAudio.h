#ifndef CYPHER_KEYS_KEY_AUDIO_H
#define CYPHER_KEYS_KEY_AUDIO_H

#include "../config/ProjectConfig.h"
#include "KeySoundPacks.h"
#include <Arduino.h>

struct HardwareProfile;  // shared/CrowPanelShared/HardwareProfile.h

// Mechanical-keyboard switch sounds for the on-screen keyboard, out of the
// NS4168 I2S amp. This is a one-shot click player, NOT a fork of project 09's
// 8-voice sample mixer: it has no sequencer, no pitch/rate conversion and no
// sample bank, because every clip plays back at 1:1 - the synthesized ones are
// rendered at the engine rate at boot, and SD packs are required to already be
// at that rate (scripts/convert-key-sounds.sh guarantees it).
//
// Sound design. Three SYNTHESIZED switch profiles, each with a press ("click")
// and a release ("clack") clip, and three slight variants of each (pitch/level/
// decay jitter plus its own noise seed) so repeated typing does not machine-gun
// one waveform:
//
//   Blue  - clicky: bright noise transient + fast ~2.6 kHz ping, loudest, with a
//           clearly audible second click on release. The "real mech" sound.
//   Brown - tactile: duller and softer (~1.2 kHz, heavily damped, much less
//           noise, more low body), quiet release.
//   Red   - linear: very quiet, mostly a soft ~110 Hz thud, barely any release.
//   Off   - silent: nothing is played and the amp is parked in its sleep state.
//
// On top of those, USE_CYPHER_KEYS_SD adds SOUND PACKS: real recorded switch
// samples read off the card into PSRAM (see KeySoundPacks.h). A pack supplies
// per-row press clips and dedicated Backspace/Enter/Space clips, so press() and
// release() take the key's class and row. The synthesized profiles ignore both
// and keep their three-variant rotation, so they behave exactly as before and
// remain the always-available default - a pack is an upgrade, never a
// requirement, and only one is ever resident (~88 KB).
//
// Threading. press()/release() are called from the loop context on the touch
// path, so they only resolve which clip to play and stamp one byte into an SPSC
// ring - no float math, no mixing, no blocking. A FreeRTOS render task mixes up
// to kVoices one-shots (rolling voice-steal), clamps, and paces itself on the
// blocking IDF i2s_channel_write, exactly as project 09's proven engine does.
// The render task NEVER touches the SD card: packs are loaded into a staging
// clip set in loop context and swapped in under a handshake that stops every
// voice first, so the retired set can be freed without a use-after-free.
//
// Without USE_CYPHER_KEYS_AUDIO (or on cores without the IDF I2S header) every
// method compiles to a no-op stub with the same signature, so HidDeck needs no
// #ifdef around its calls and ready() simply reports false.
class KeyAudio {
 public:
  enum Profile : uint8_t { kOff = 0, kBlue, kBrown, kRed, kProfileCount };

  // Which clip a key wants out of a sound pack. Values alias
  // KeySoundPacks::KeyClass, which is also what HidKeyboard::keySoundClass()
  // returns, so the three never drift apart.
  enum KeyClass : uint8_t {
    kKeyGeneric = KeySoundPacks::kClassGeneric,
    kKeyBackspace = KeySoundPacks::kClassBackspace,
    kKeyEnter = KeySoundPacks::kClassEnter,
    kKeySpace = KeySoundPacks::kClassSpace,
  };

  bool begin(const HardwareProfile &profile, Print &log);

  // key touch-DOWN (a real switch actuates here) and key lift. `row` is the
  // on-screen keyboard row, 0 at the top; both arguments only matter for SD
  // packs.
  void press(KeyClass k, uint8_t row);
  void release(KeyClass k, uint8_t row);

  void setProfile(Profile p);  // persists; also deselects any SD pack
  Profile profile() const { return profile_; }
  const char *profileName() const;
  bool selectProfileByName(const String &name);  // prefix match
  // There is deliberately no nextProfile() here: cycling has to walk the SD
  // packs too, which needs the card, so HidDeck::stepSound() owns it.

  void setVolume(uint8_t pct);  // 0..100, persists
  uint8_t volume() const { return volume_; }

  bool ready() const { return running_; }  // true when I2S is live
  String status() const;

  // --- SD sound packs (all loop context; no-ops in a silent build) -----------

  // Clears and hands out the inactive clip set for KeySoundPacks::loadPack to
  // fill. The render task never reads this set, so filling it is safe while
  // audio keeps playing. Returns nullptr when NEITHER set can be reused safely,
  // which only happens if an earlier swap was never acknowledged (a wedged
  // render task) - refusing the load beats freeing PCM the mixer may still be
  // reading.
  KeySoundPacks::ClipSet *beginPackStaging();

  // Makes the staged set live and selects it, then frees the set it replaced.
  // Appends what happened to `status`. False (and staging freed) if the pack has
  // no usable press clip. Only valid after a non-null beginPackStaging().
  bool commitPackStaging(const char *name, String &status);
  void discardPackStaging();

  bool packActive() const { return packSelected_ && livePack_ < 2; }
  const char *packName() const;  // resident pack, "" when none
  const char *soundName() const;  // pack name if active, else the profile name

  // Pack chosen on a previous boot (NVS "sndpack"), for HidDeck to re-load once
  // the card is up. Empty when a synthesized profile was selected.
  const char *storedPackName() const { return storedPack_; }
  // Last pack load outcome, appended to status() so a missing pack is visible.
  // Fixed buffers rather than Strings: this class is a global, and a POD-ish
  // one costs a silent build nothing at all.
  void setPackStatus(const String &text) {
    const size_t room = sizeof(packStatus_) - 1;
    const size_t n = text.length() < room ? text.length() : room;
    memcpy(packStatus_, text.c_str(), n);
    packStatus_[n] = '\0';
  }

 private:
  static const uint8_t kRealProfiles = 3;  // Blue, Brown, Red (Off makes none)
  static const uint8_t kVariants = 3;      // jittered copies of each clip
  static const uint8_t kVoices = 4;        // concurrent one-shots
  static const uint8_t kClipCount = kRealProfiles * 2 * kVariants;
  static const uint8_t kTrigRing = 16;     // power of two
  // Trigger-ring tag: set means "slot in the live pack", clear means "index into
  // the synthesized clips". kSlotCount is 12, so the low bits never overflow.
  static const uint8_t kPackFlag = 0x80;
  // Longest the swap handshake waits for the render task. One DMA block is
  // ~6 ms, so this is generous; past it the pack is adopted but the retired
  // buffers are deliberately leaked rather than risk freeing under the mixer.
  static const uint32_t kSwapWaitMs = 120;

  struct Clip {
    int16_t *pcm = nullptr;
    uint32_t frames = 0;
  };

  // Render-task state. No resampling: clips are at the engine rate, so a voice
  // is just a cursor plus a gain.
  struct Voice {
    const int16_t *pcm = nullptr;
    uint32_t frames = 0;
    uint32_t pos = 0;
    int32_t gainQ12 = 0;
    bool active = false;
  };

  void loadPrefs_();
  void persistPrefs_() const;
  bool synthesizeAll_(Print &log);
  void setAmp_(bool on);
  void trigger_(bool releaseSound, KeyClass k, uint8_t row);
  void push_(uint8_t tag);
  void drainTriggers_();
  void servicePackSwap_();  // render task side of the clip-set swap
  uint8_t stagingIndex_() const { return livePack_ == 0 ? 1 : 0; }
  void renderTask_();
  static void renderTrampoline_(void *self);

  Clip clips_[kClipCount];
  Voice voices_[kVoices];

  // Double-buffered SD packs: loop context fills packs_[stagingIndex_()] while
  // the render task plays packs_[activePack_]. Only one is ever populated at a
  // time in steady state - the other is freed right after a swap.
  KeySoundPacks::ClipSet packs_[2];

  // Loop context writes trigHead_, render task writes trigTail_.
  volatile uint8_t trigRing_[kTrigRing];
  volatile uint8_t trigHead_ = 0;
  volatile uint8_t trigTail_ = 0;

  // Swap handshake. Loop bumps swapRequest_ after publishing swapTo_; the render
  // task kills every voice, adopts it, and copies the ticket into swapAck_.
  volatile uint8_t swapRequest_ = 0;
  volatile uint8_t swapAck_ = 0;
  volatile uint8_t swapTo_ = 0;
  volatile uint8_t activePack_ = 0xFF;  // render task's view; 0xFF = none

  volatile Profile profile_ = kBlue;
  volatile uint8_t volume_ = CYPHER_KEYS_AUDIO_VOLUME;
  volatile bool running_ = false;

  // Loop-context only.
  uint8_t livePack_ = 0xFF;    // loop's mirror of activePack_
  bool packSelected_ = false;  // pack, rather than a synthesized profile
  char storedPack_[KeySoundPacks::kMaxNameLen] = {0};
  char packStatus_[112] = {0};
  uint8_t nextVariant_ = 0;
  uint32_t jitter_ = 0x1F123BB5;
  uint32_t triggered_ = 0;
  uint32_t dropped_ = 0;
  uint32_t packHits_ = 0;  // clicks served from the card, for status()
  uint8_t ampPin_ = 0;
  bool ampActiveHigh_ = false;
  bool ampOn_ = false;

  uint8_t voiceCursor_ = 0;  // render-task only (voice-steal rotation)
  void *txChan_ = nullptr;   // i2s_chan_handle_t, opaque here
  void *task_ = nullptr;     // TaskHandle_t
};

#endif
