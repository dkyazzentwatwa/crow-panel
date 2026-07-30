#ifndef POKEDEX_SD_SOURCE_H
#define POKEDEX_SD_SOURCE_H

#include "../config/ProjectConfig.h"
#include "PokedexIndex.h"

#if USE_SD_POKEDEX
// Include SD_MMC.h directly under the flag - do NOT wrap it in __has_include.
// SD_MMC is on the include path, so a __has_include guard evaluates false at the
// wrong moment and silently disables the feature while still building green.
#include <SD_MMC.h>

// Adapts an SD_MMC File to the pure-C++ ByteSource the index consumes. This is
// the only place SD_MMC and PokedexIndex meet.
class PokedexSdSource : public pokedex::ByteSource {
 public:
  explicit PokedexSdSource(File &file) : file_(file) {}
  bool seek(uint32_t offset) override { return file_.seek(offset); }
  uint32_t read(uint8_t *dest, uint32_t length) override {
    return (uint32_t)file_.read(dest, length);
  }
  uint32_t size() const override { return (uint32_t)file_.size(); }

 private:
  File &file_;
};
#endif

#endif
