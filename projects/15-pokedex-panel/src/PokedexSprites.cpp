#include "PokedexSprites.h"

// Include SD_MMC.h directly under the flag - do NOT wrap it in __has_include.
// arduino-cli decides which libraries to link by preprocessing sources before
// SD_MMC is on the include path, so a __has_include guard evaluates false
// during discovery, the library never gets linked, and the feature silently
// compiles out even with the flag on. See PokedexCatalog.cpp / PokedexSdSource.h
// for the same pattern.
#if USE_POKEDEX_SPRITES && USE_SD_POKEDEX
#define POKEDEX_SPRITES_HAVE_SD 1
#include <SD_MMC.h>
#else
#define POKEDEX_SPRITES_HAVE_SD 0
#endif

#include "Logger.h"

#include <string.h>

namespace {
const uint32_t kFileBufferSize = 8192;
const int32_t kSpriteW = 40;
const int32_t kSpriteH = 40;
}  // namespace

void PokedexSprites::begin() {
  tileSize_ = kSpriteW * POKEDEX_SPRITE_TILE_SCALE;
  heroSize_ = kSpriteH * POKEDEX_SPRITE_HERO_SCALE;
  ready_ = false;

#if POKEDEX_SPRITES_HAVE_SD
  fileBuffer_ = (uint8_t *)ps_malloc(kFileBufferSize);
  decodeBuffer_ = (uint16_t *)ps_malloc((uint32_t)kSpriteW * kSpriteH * sizeof(uint16_t));
  heroPixels_ = (uint16_t *)ps_malloc((uint32_t)heroSize_ * heroSize_ * sizeof(uint16_t));

  if (fileBuffer_ == nullptr || decodeBuffer_ == nullptr || heroPixels_ == nullptr) {
    Logger::warn("sprites", "PSRAM alloc failed for core sprite buffers; sprites disabled");
    return;
  }

  const uint32_t tilePixels = (uint32_t)tileSize_ * tileSize_;
  for (uint8_t i = 0; i < POKEDEX_SPRITE_CACHE_SLOTS; i++) {
    slots_[i].pixels = (uint16_t *)ps_malloc(tilePixels * sizeof(uint16_t));
    if (slots_[i].pixels == nullptr) {
      Logger::warn("sprites", String("PSRAM alloc failed for tile slot ") + i);
    }
  }

  ready_ = true;
#else
  (void)kFileBufferSize;
#endif
}

bool PokedexSprites::loadScaled(const char *entryId, uint8_t scale, uint16_t *dest,
                                 uint32_t destPixels, uint16_t &outKey) {
#if POKEDEX_SPRITES_HAVE_SD
  String path = String(POKEDEX_SPRITE_DIR) + entryId + ".bmp";
  File file = SD_MMC.open(path.c_str(), FILE_READ);
  if (!file) {
    Logger::warn("sprites", String("cannot open sprite ") + entryId);
    failures_++;
    return false;
  }

  uint32_t size = (uint32_t)file.size();
  if (size == 0 || size > kFileBufferSize) {
    Logger::warn("sprites", String("sprite ") + entryId + " has bad size");
    file.close();
    failures_++;
    return false;
  }

  uint32_t got = (uint32_t)file.read(fileBuffer_, size);
  file.close();
  if (got != size) {
    Logger::warn("sprites", String("sprite ") + entryId + " short read");
    failures_++;
    return false;
  }

  pokedex::BmpInfo info;
  if (!pokedex::readBmpInfo(fileBuffer_, size, info)) {
    Logger::warn("sprites", String("sprite ") + entryId + " has bad BMP header");
    failures_++;
    return false;
  }
  if (info.width != kSpriteW || info.height != kSpriteH) {
    Logger::warn("sprites", String("sprite ") + entryId + " is not 40x40");
    failures_++;
    return false;
  }

  uint32_t t0 = micros();
  if (!pokedex::decodeBmp(fileBuffer_, size, decodeBuffer_, (uint32_t)kSpriteW * kSpriteH)) {
    Logger::warn("sprites", String("sprite ") + entryId + " failed to decode");
    failures_++;
    return false;
  }

  outKey = pokedex::backgroundKey(decodeBuffer_, kSpriteW, kSpriteH);

  if (!pokedex::upscaleNearest(decodeBuffer_, kSpriteW, kSpriteH, scale, dest, destPixels)) {
    Logger::warn("sprites", String("sprite ") + entryId + " failed to upscale");
    failures_++;
    return false;
  }
  lastDecodeMicros_ = micros() - t0;
  return true;
#else
  (void)entryId;
  (void)scale;
  (void)dest;
  (void)destPixels;
  (void)outKey;
  return false;
#endif
}

int8_t PokedexSprites::findSlot(const char *entryId) const {
  for (uint8_t i = 0; i < POKEDEX_SPRITE_CACHE_SLOTS; i++) {
    if (slots_[i].pixels != nullptr && slots_[i].entryId[0] != '\0' &&
        strcmp(slots_[i].entryId, entryId) == 0) {
      return (int8_t)i;
    }
  }
  return -1;
}

int8_t PokedexSprites::evictSlot() {
  int8_t best = -1;
  uint32_t bestUsed = 0xFFFFFFFFu;
  for (uint8_t i = 0; i < POKEDEX_SPRITE_CACHE_SLOTS; i++) {
    if (slots_[i].pixels == nullptr) continue;
    if (best < 0 || slots_[i].lastUsed < bestUsed) {
      best = (int8_t)i;
      bestUsed = slots_[i].lastUsed;
    }
  }
  return best;
}

const uint16_t *PokedexSprites::tile(const char *entryId, uint16_t *outKey) {
  if (!ready_ || entryId == nullptr || entryId[0] == '\0') return nullptr;

  clock_++;
  int8_t idx = findSlot(entryId);
  if (idx >= 0) {
    Slot &slot = slots_[idx];
    slot.lastUsed = clock_;
    hits_++;
    if (outKey != nullptr) *outKey = slot.key;
    return slot.pixels;
  }

  idx = evictSlot();
  if (idx < 0) return nullptr;  // every slot failed to allocate at begin()

  Slot &slot = slots_[idx];
  uint16_t key = 0;
  if (!loadScaled(entryId, POKEDEX_SPRITE_TILE_SCALE, slot.pixels,
                  (uint32_t)tileSize_ * tileSize_, key)) {
    slot.entryId[0] = '\0';
    return nullptr;
  }

  misses_++;
  strncpy(slot.entryId, entryId, sizeof(slot.entryId) - 1);
  slot.entryId[sizeof(slot.entryId) - 1] = '\0';
  slot.key = key;
  slot.lastUsed = clock_;
  if (outKey != nullptr) *outKey = key;
  return slot.pixels;
}

const uint16_t *PokedexSprites::hero(const char *entryId, uint16_t *outKey) {
  if (!ready_ || heroPixels_ == nullptr || entryId == nullptr || entryId[0] == '\0') {
    return nullptr;
  }

  if (heroEntry_[0] != '\0' && strcmp(heroEntry_, entryId) == 0) {
    hits_++;
    if (outKey != nullptr) *outKey = heroKey_;
    return heroPixels_;
  }

  uint16_t key = 0;
  if (!loadScaled(entryId, POKEDEX_SPRITE_HERO_SCALE, heroPixels_,
                  (uint32_t)heroSize_ * heroSize_, key)) {
    heroEntry_[0] = '\0';
    return nullptr;
  }

  misses_++;
  strncpy(heroEntry_, entryId, sizeof(heroEntry_) - 1);
  heroEntry_[sizeof(heroEntry_) - 1] = '\0';
  heroKey_ = key;
  if (outKey != nullptr) *outKey = key;
  return heroPixels_;
}

void PokedexSprites::printDiagnostics(Print &out) const {
  out.print(F("sprites "));
  out.print(ready_ ? F("ready") : F("off"));
  out.print(F(" hit=")); out.print(hits_);
  out.print(F(" miss=")); out.print(misses_);
  out.print(F(" fail=")); out.print(failures_);
  out.print(F(" us=")); out.println(lastDecodeMicros_);
}
