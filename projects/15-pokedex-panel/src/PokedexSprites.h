#ifndef POKEDEX_SPRITES_H
#define POKEDEX_SPRITES_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>
#include "PokedexBmp.h"

// Loads sprite BMPs off SD, upscales them, and keeps one grid page of tiles plus
// one hero image in PSRAM. All buffers are PSRAM; a failed allocation degrades to
// "no sprite" rather than aborting, and the UI falls back to the drawn pokeball.
//
// Sprite BMPs have no alpha, so each cached entry also carries the background
// colour sampled from its border. The caller keys that colour out at blit time
// with draw16bitRGBBitmapWithTranColor over a pure-black sprite well.
class PokedexSprites {
 public:
  void begin();

  // Returns a tile-scaled RGB565 buffer for entryId, or nullptr when the sprite
  // is missing, malformed, or could not be cached. On success, outKey receives
  // the colour the caller must treat as transparent.
  const uint16_t *tile(const char *entryId, uint16_t *outKey);
  int16_t tileSize() const { return tileSize_; }

  // Returns the hero-scaled buffer for entryId, or nullptr. Only one hero is
  // resident at a time; requesting a different entry replaces it.
  const uint16_t *hero(const char *entryId, uint16_t *outKey);
  int16_t heroSize() const { return heroSize_; }

  bool ready() const { return ready_; }
  uint32_t hits() const { return hits_; }
  uint32_t misses() const { return misses_; }
  uint32_t failures() const { return failures_; }
  uint32_t lastDecodeMicros() const { return lastDecodeMicros_; }
  void printDiagnostics(Print &out) const;

 private:
  struct Slot {
    char entryId[48] = "";
    uint16_t *pixels = nullptr;
    uint16_t key = 0;
    uint32_t lastUsed = 0;
  };

  bool ready_ = false;
  int16_t tileSize_ = 0;
  int16_t heroSize_ = 0;
  uint32_t clock_ = 0;
  uint32_t hits_ = 0;
  uint32_t misses_ = 0;
  uint32_t failures_ = 0;
  uint32_t lastDecodeMicros_ = 0;

  Slot slots_[POKEDEX_SPRITE_CACHE_SLOTS];
  char heroEntry_[48] = "";
  uint16_t *heroPixels_ = nullptr;
  uint16_t heroKey_ = 0;
  uint8_t *fileBuffer_ = nullptr;
  uint16_t *decodeBuffer_ = nullptr;

  // Loads entryId, upscales by `scale` into dest, and reports the background key.
  bool loadScaled(const char *entryId, uint8_t scale, uint16_t *dest,
                  uint32_t destPixels, uint16_t &outKey);
  int8_t findSlot(const char *entryId) const;
  int8_t evictSlot();
};

#endif
