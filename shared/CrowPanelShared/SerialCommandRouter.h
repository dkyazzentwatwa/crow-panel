#ifndef CROW_PANEL_SERIAL_COMMAND_ROUTER_H
#define CROW_PANEL_SERIAL_COMMAND_ROUTER_H

#include <Arduino.h>

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
  static const uint8_t kMaxCommands = 12;
  static const uint8_t kMaxLineLen = 96;

  void begin(Stream &io, const char *appName);

  // Register a command. Returns false (and logs) if the table is full.
  bool on(const char *name, const char *help, SerialCommandHandler handler);

  // Call once per loop().
  void poll();

  void printHelp() const;

 private:
  struct Command {
    const char *name;
    const char *help;
    SerialCommandHandler handler;
  };

  void dispatchLine();

  Command commands_[kMaxCommands];
  uint8_t commandCount_ = 0;
  char line_[kMaxLineLen];
  uint8_t lineLen_ = 0;
  bool discardingOverflow_ = false;
  Stream *io_ = nullptr;
  const char *appName_ = "app";
};

#endif
