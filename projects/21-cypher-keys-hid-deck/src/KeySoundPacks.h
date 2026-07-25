#ifndef CYPHER_KEYS_KEY_SOUND_PACKS_H
#define CYPHER_KEYS_KEY_SOUND_PACKS_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// SD-card sound packs for the key-click engine: REAL recorded mechanical switch
// samples, loaded off the card into PSRAM and played instead of KeyAudio's
// synthesized profiles. Guarded by USE_CYPHER_KEYS_SD; without it every entry
// point below is a stub and nothing here reaches the binary.
//
// Layout on the card (see scripts/convert-key-sounds.sh, which produces exactly
// this from a kbsim-style tree):
//
//   CYPHER_KEYS_SOUNDS_DIR/<pack>/press/GENERIC_R0.wav ... GENERIC_R4.wav
//   CYPHER_KEYS_SOUNDS_DIR/<pack>/press/{BACKSPACE,ENTER,SPACE}.wav
//   CYPHER_KEYS_SOUNDS_DIR/<pack>/release/GENERIC.wav
//   CYPHER_KEYS_SOUNDS_DIR/<pack>/release/{BACKSPACE,ENTER,SPACE}.wav
//
// Real packs are ragged, so only press/GENERIC_R0.wav is required and unknown
// filenames are ignored (kbsim's `mxblue` ships nothing but GENERIC_R0..R4 plus
// release/GENERIC; its `bluealps` has an extra release/GENERIC_long.wav).
// resolvePressSlot()/resolveReleaseSlot() below encode the fallback order that
// makes such a pack work, and are pure functions so the host tests can prove it.
//
// THREADING: every function here does SD I/O and MUST be called from loop
// context only. The KeyAudio render task never touches the card - it is handed a
// fully loaded ClipSet and swaps to it (see KeyAudio::commitPackStaging).
namespace KeySoundPacks {

// Which clip a key wants. Numbering is shared with KeyAudio::KeyClass (whose
// enumerators alias these) and with HidKeyboard::keySoundClass().
enum KeyClass : uint8_t {
  kClassGeneric = 0,
  kClassBackspace,
  kClassEnter,
  kClassSpace,
  kClassCount,
};

// One slot per clip a pack can supply. Press clips first (five row variants
// then the three special keys), then the release clips.
enum Slot : uint8_t {
  kPressR0 = 0,
  kPressR1,
  kPressR2,
  kPressR3,
  kPressR4,
  kPressBackspace,
  kPressEnter,
  kPressSpace,
  kReleaseGeneric,
  kReleaseBackspace,
  kReleaseEnter,
  kReleaseSpace,
  kSlotCount,
};
static const uint8_t kSlotNone = 0xFF;   // "no clip; fall back to synthesized"
static const uint8_t kPressRows = 5;     // GENERIC_R0..R4
static const uint8_t kMaxPacks = 16;     // most pack folders the UI will cycle
static const uint8_t kMaxNameLen = 24;   // pack folder name, including the NUL

// One decoded clip. PCM is int16 mono at CYPHER_KEYS_AUDIO_SAMPLE_RATE, in
// PSRAM when available, owned by the ClipSet.
struct Clip {
  int16_t *pcm = nullptr;
  uint32_t frames = 0;
};

// A whole pack: the clips plus a bitmask of which slots are actually filled, so
// the resolution order can be evaluated without dereferencing anything.
struct ClipSet {
  Clip slots[kSlotCount];
  uint16_t present = 0;  // bit (1 << slot) per loaded clip
  uint8_t clips = 0;     // popcount(present), for status text
  uint32_t bytes = 0;    // PCM bytes held
  char name[kMaxNameLen] = {0};

  bool has(uint8_t slot) const {
    return slot < kSlotCount && (present & (uint16_t)(1u << slot)) != 0;
  }
  // Frees every PCM buffer and resets to empty. Loop context only, and never
  // while the render task could still be reading this set.
  void freeAll();
  void setName(const char *n);
};

// --- pure logic (no SD, no Arduino state): compiled in every build ------------

// Press clip for a key: the class-specific clip, else this row's GENERIC_R<row>,
// else GENERIC_R0, else kSlotNone (caller plays the synthesized click).
uint8_t resolvePressSlot(uint16_t present, uint8_t keyClass, uint8_t row);

// Release clip for a key: the class-specific clip, else GENERIC, else kSlotNone.
uint8_t resolveReleaseSlot(uint16_t present, uint8_t keyClass);

// Maps a file inside a pack's press/ or release/ folder to its slot.
// `phase` is "press" or "release"; `fileName` may keep its .wav extension and is
// matched case-insensitively. Returns kSlotNone for anything unrecognized, which
// is how release/GENERIC_long.wav and stray files are ignored.
uint8_t slotForFile(bool releasePhase, const char *fileName);

// What a WAV's header says, once it has been accepted.
struct WavInfo {
  uint32_t rate = 0;
  uint32_t dataOffset = 0;  // byte offset of the PCM payload
  uint32_t dataBytes = 0;   // payload length as declared by the data chunk
  uint16_t channels = 0;
  uint16_t bits = 0;
};

// Walks the RIFF chunks in `head` (the first `len` bytes of a file) and accepts
// only 16-bit PCM mono at CYPHER_KEYS_AUDIO_SAMPLE_RATE - the engine plays clips
// 1:1, so anything else would be the wrong pitch. `why` gets a short reason on
// rejection. Pure and buffer-based so the host tests can feed it real files.
bool parseWavHeader(const uint8_t *head, uint32_t len, WavInfo &out, String &why);

// --- SD-backed (stubs without USE_CYPHER_KEYS_SD) ----------------------------

// Mounts SD_MMC once; idempotent, safe to call on every access. Returns
// card-present. Call it BEFORE CrowDisplay::begin(): mounting SD_MMC after the
// MIPI-DSI framebuffer is live can leave this panel backlit but blank (the
// device-proven order from projects 02 and 20).
bool beginSd();
bool sdReady();

// Pack folder names under CYPHER_KEYS_SOUNDS_DIR, sorted, dotfiles skipped.
// Returns how many names were written to `out`.
uint8_t listPacks(String *out, uint8_t maxPacks);

// Loads one pack into `out` (which must be empty - use
// KeyAudio::beginPackStaging()). Returns true when the pack is playable, i.e. it
// has at least press/GENERIC_R0.wav. `status` always describes the result,
// including per-file rejections and how long the read took.
bool loadPack(const char *name, ClipSet &out, String &status);

}  // namespace KeySoundPacks

#endif
