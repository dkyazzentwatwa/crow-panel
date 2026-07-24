#ifndef CYPHER_BOY_ROM_STORE_H
#define CYPHER_BOY_ROM_STORE_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Owns the SD card and the /roms + /saves layout.
//
// gnuboy does its own ROM and battery-save file I/O with C stdio, so the card
// is mounted through the FAT VFS at GB_SD_ROOT ("/sdcard") and this class only
// has to enumerate ROMs and hand out paths - it never reads cartridge bytes
// itself.
//
// With USE_GB_SD=0 (the default) no card is touched: a built-in placeholder
// list is reported so the picker screen and the selftest still work on a bare
// board. ready() tells you which mode you got.
class GbRomStore {
 public:
  static const uint8_t kMaxRoms = 32;

  bool begin();
  uint8_t count() const { return count_; }
  const String &name(uint8_t index) const;

  // GB_ROM_DIR "/" name - the path handed to gnuboy_load_rom_file().
  String romPath(uint8_t index) const;
  // GB_SAVE_DIR "/" basename ".sav" - handed to gnuboy_load_sram/save_sram().
  String savePath(uint8_t index) const;

  // True when a real SD card is mounted; false when serving the placeholder
  // list. Callers must not treat a placeholder path as loadable.
  bool ready() const { return ready_; }
  const String &status() const { return status_; }

 private:
  void seedPlaceholder();

  String names_[kMaxRoms];
  String empty_;
  uint8_t count_ = 0;
  bool ready_ = false;
  String status_;
};

#endif
