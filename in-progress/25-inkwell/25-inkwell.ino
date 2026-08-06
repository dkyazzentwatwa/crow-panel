// SCAFFOLD ONLY. No parsing, no display, no SD. This sketch exists to prove
// the project compiles against the shared Serial UX before Task 2+ add the
// text pipeline, EPUB support, and the portrait DSI render path.

#include "config/ProjectConfig.h"
#include <CrowPanelShared.h>

SerialCommandRouter router;
EventLog eventLog;

void cmdStatus(const String &) {
  printSystemStatus(Serial, "inkwell", eventLog.size(), &router);
}

void cmdHistory(const String &) { eventLog.printHistory(Serial); }

void cmdBooks(const String &) {
  Serial.println(F("library: (empty scaffold — Task 8 adds sample books)"));
}

void setup() {
  Logger::begin(115200);
  Logger::info("app", "Inkwell — portrait e-ink-style reader (scaffold)");
  printHardwareProfile(Serial, activeHardwareProfile());
  eventLog.add("Inkwell booted");

  router.begin(Serial, "inkwell");
  router.on("status", "scaffold and proof status", cmdStatus, "system");
  router.on("history", "recent event history", cmdHistory, "system");
  router.on("books", "list the library (scaffold only)", cmdBooks, "library");
}

void loop() {
  router.poll();
  delay(1);
}
