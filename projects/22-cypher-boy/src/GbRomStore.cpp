#include "GbRomStore.h"

// NOTE: <SD_MMC.h> is included directly, NOT behind __has_include.
// arduino-cli discovers which libraries to link by preprocessing the sources
// and following include directives (it does honour -D flags from
// compiler.cpp.extra_flags, so `#if USE_GB_SD` is fine). But an
// __has_include(<SD_MMC.h>) guard defeats itself: during the discovery pass
// SD_MMC is not on the include path yet, so __has_include is false, the
// include is skipped, the library is never added - and the SD support is then
// silently compiled out even with -DUSE_GB_SD=1. The shared DisplayBringup.cpp
// includes <Arduino_GFX_Library.h> unguarded for exactly this reason.
#if USE_GB_SD
#include <SD_MMC.h>
#define CYPHER_BOY_HAS_SD_MMC 1
#else
#define CYPHER_BOY_HAS_SD_MMC 0
#endif

#include <CrowPanelShared.h>

// Default SD_MMC mount mode for conservative bring-up (1-bit bus), matching
// the convention used by projects 08 and 15.
#ifndef CYPHER_BOY_SDMMC_1BIT
#define CYPHER_BOY_SDMMC_1BIT 1
#endif

namespace {

bool hasRomSuffix(const String &name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".gb") || lower.endsWith(".gbc");
}

// "/sdcard/roms/Pokemon.gb" or "Pokemon.gb" -> "Pokemon"
String baseName(const String &path) {
  int slash = path.lastIndexOf('/');
  String file = (slash >= 0) ? path.substring(slash + 1) : path;
  int dot = file.lastIndexOf('.');
  return (dot > 0) ? file.substring(0, dot) : file;
}

}  // namespace

void GbRomStore::seedPlaceholder() {
  names_[0] = "no-sd-placeholder.gb";
  count_ = 1;
  ready_ = false;
}

bool GbRomStore::begin() {
  count_ = 0;
  ready_ = false;

#if USE_GB_SD && CYPHER_BOY_HAS_SD_MMC
  // Don't re-mount a card another subsystem already brought up - SD_MMC.begin()
  // on an live mount fails and would drop us to the placeholder list for no
  // reason. Same guard Cypher Desk (project 18) uses.
  bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  if (!alreadyMounted && !SD_MMC.begin(GB_SD_ROOT, CYPHER_BOY_SDMMC_1BIT != 0)) {
    status_ = "SD_MMC mount failed; placeholder list active";
    Logger::error("romstore", status_);
    seedPlaceholder();
    return false;
  }

  // NOTE the *_FS paths here. The Arduino FS API is relative to the mount
  // point (FS/vfs_api.cpp does snprintf("%s%s", _mountpoint, path)), so
  // passing the full "/sdcard/roms" would resolve to "/sdcard/sdcard/roms"
  // and fail even though the card mounted perfectly. gnuboy's stdio calls
  // still want the full GB_ROM_DIR / GB_SAVE_DIR paths - see romPath()/savePath().
  if (!SD_MMC.exists(GB_ROM_DIR_FS)) {
    SD_MMC.mkdir(GB_ROM_DIR_FS);
  }
  if (!SD_MMC.exists(GB_SAVE_DIR_FS)) {
    SD_MMC.mkdir(GB_SAVE_DIR_FS);
  }
  if (!SD_MMC.exists(GB_STATE_DIR_FS)) {
    SD_MMC.mkdir(GB_STATE_DIR_FS);
  }

  File dir = SD_MMC.open(GB_ROM_DIR_FS);
  if (!dir || !dir.isDirectory()) {
    status_ = String("mounted, but cannot open ") + GB_ROM_DIR_FS +
              " on the card; placeholder list active";
    Logger::error("romstore", status_);
    seedPlaceholder();
    return false;
  }

  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      continue;
    }
    String full = String(entry.name());
    int slash = full.lastIndexOf('/');
    String justFile = (slash >= 0) ? full.substring(slash + 1) : full;
    if (!hasRomSuffix(justFile)) {
      continue;
    }
    if (count_ >= kMaxRoms) {
      Logger::error("romstore", String("more than ") + kMaxRoms + " ROMs; extras ignored");
      break;
    }
    names_[count_++] = justFile;
  }
  dir.close();

  ready_ = true;
  // Report the on-card folder (roms/), not the VFS path - that is what the
  // user actually sees when they put the card in a computer.
  status_ = String("SD ready, ") + count_ + " ROM(s) in " + GB_ROM_DIR_FS;
  Logger::info("romstore", status_);
  if (count_ == 0) {
    status_ = String("SD ready but no .gb/.gbc found in ") + GB_ROM_DIR_FS;
  }
  return true;
#else
  status_ = "USE_GB_SD=0; placeholder list (no card read)";
  Logger::info("romstore", status_);
  seedPlaceholder();
  return false;
#endif
}

const String &GbRomStore::name(uint8_t index) const {
  if (index >= count_) {
    return empty_;
  }
  return names_[index];
}

String GbRomStore::romPath(uint8_t index) const {
  if (index >= count_) {
    return String();
  }
  return String(GB_ROM_DIR) + "/" + names_[index];
}

String GbRomStore::savePath(uint8_t index) const {
  if (index >= count_) {
    return String();
  }
  return String(GB_SAVE_DIR) + "/" + baseName(names_[index]) + ".sav";
}

String GbRomStore::statePath(uint8_t index, uint8_t slot) const {
  if (index >= count_) {
    return String();
  }
  return String(GB_STATE_DIR) + "/" + baseName(names_[index]) + ".st" + String(slot);
}
