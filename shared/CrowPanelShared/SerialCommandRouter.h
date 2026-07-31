#ifndef CROW_PANEL_SERIAL_COMMAND_ROUTER_H
#define CROW_PANEL_SERIAL_COMMAND_ROUTER_H

#include <Arduino.h>
#include "AppConfig.h"

// Handler receives everything typed after the command word, trimmed
// (may be empty). Plain function pointers on purpose: sketch globals are
// directly reachable from free-function handlers, which keeps the wiring
// simple enough to show on camera.
typedef void (*SerialCommandHandler)(const String &args);

// Minimal line-based command dispatcher for Serial demos.
//
// Non-blocking: poll() consumes only the bytes already available and
// dispatches when a full '\n'-terminated line arrives (set the Serial
// monitor line ending to Newline). No Serial.readString(), no timeouts.
class SerialCommandRouter {
 public:
  // Table size lives in AppConfig.h so it is one number for the whole repo -
  // see the comment there for why this must NOT be overridden per project.
  static const uint16_t kMaxCommands = CROW_SERIAL_MAX_COMMANDS;
  static const uint8_t kMaxLineLen = 96;

  void begin(Stream &io, const char *appName);

  // Register a command. Returns false (and logs) if the table is full.
  // `group` is optional: when several commands share a group name, printHelp()
  // prints them under one heading, which keeps a 30-command sketch readable.
  // Passing nullptr keeps the flat listing.
  bool on(const char *name, const char *help, SerialCommandHandler handler,
          const char *group = nullptr);

  // Call once per loop().
  void poll();

  void printHelp() const;

  uint16_t commandCount() const { return commandCount_; }
  // Registrations refused because the table was full. Non-zero means commands
  // the sketch believes it registered will never dispatch; begin()/printHelp()
  // and StatusReport both surface it so it cannot go unnoticed again.
  uint16_t droppedCount() const { return droppedCount_; }

 private:
  struct Command {
    const char *name;
    const char *help;
    SerialCommandHandler handler;
    const char *group;
  };

  void dispatchLine();

  Command commands_[kMaxCommands];
  uint16_t commandCount_ = 0;
  uint16_t droppedCount_ = 0;
  bool droppedWarned_ = false;
  char line_[kMaxLineLen];
  uint8_t lineLen_ = 0;
  bool discardingOverflow_ = false;
  Stream *io_ = nullptr;
  const char *appName_ = "app";
};

#endif
