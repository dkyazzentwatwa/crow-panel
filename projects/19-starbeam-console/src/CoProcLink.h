#ifndef STARBEAM_CONSOLE_COPROC_LINK_H
#define STARBEAM_CONSOLE_COPROC_LINK_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"
#include "StarbeamTypes.h"

// UART client for the ESP32 dev module that runs the Wi-Fi/BLE/attack half of
// Starbeam (stock starbeam_v2, whose terminal.cpp exposes every feature as a
// serial command). The panel writes "<command>\n" and reads back the module's
// serial output, marking the link live whenever a line arrives. If the module
// firmware is patched to emit a compact "TLM key=value ..." line, this parses
// it into CoProcSnapshot for live counters/heatmap; otherwise it still shows
// the module's most recent line as status text.
//
// Compiled behind USE_STARBEAM_COPROC=1; a no-op stub keeps other builds green.

class CoProcLink {
 public:
  void begin();
  void send(const char *command);     // forwards a Starbeam terminal command
  void poll(CoProcSnapshot &snap);     // read lines, update link + telemetry
  bool linked() const { return linked_; }

 private:
#if USE_STARBEAM_COPROC
  void parseLine_(const String &line, CoProcSnapshot &snap);
  String rx_;
#endif
  bool linked_ = false;
  uint32_t lastPingMs_ = 0;
};

#endif  // STARBEAM_CONSOLE_COPROC_LINK_H
