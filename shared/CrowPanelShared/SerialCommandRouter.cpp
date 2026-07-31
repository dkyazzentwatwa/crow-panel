#include "SerialCommandRouter.h"
#include "Logger.h"
#include <string.h>  // strcmp, for the printHelp() group headings

void SerialCommandRouter::begin(Stream &io, const char *appName) {
  io_ = &io;
  appName_ = appName;
  lineLen_ = 0;
  Logger::info("cmd", String(appName_) + " serial commands ready, type: help");
}

bool SerialCommandRouter::on(const char *name, const char *help, SerialCommandHandler handler,
                            const char *group) {
  if (commandCount_ >= kMaxCommands) {
    droppedCount_++;
    Logger::error("cmd", String("command table full (") + kMaxCommands + "), dropped: " + name);
    return false;
  }
  commands_[commandCount_++] = {name, help, handler, group};
  return true;
}

void SerialCommandRouter::poll() {
  if (io_ == nullptr) {
    return;
  }
  // Registration happens after begin(), so the first poll() - one loop() tick
  // after setup() - is the earliest honest place to report a full table. This
  // has to be impossible to miss: a silently dropped command looks exactly
  // like a command that exists but does nothing.
  if (droppedCount_ > 0 && !droppedWarned_) {
    droppedWarned_ = true;
    Logger::error("cmd", String(droppedCount_) + " command(s) never registered (table full at " +
                             kMaxCommands + ") - they will NOT respond. Raise " +
                             "CROW_SERIAL_MAX_COMMANDS in AppConfig.h.");
  }
  while (io_->available() > 0) {
    char c = (char)io_->read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (discardingOverflow_) {
        discardingOverflow_ = false;
      } else if (lineLen_ > 0) {
        line_[lineLen_] = '\0';
        dispatchLine();
      }
      lineLen_ = 0;
      continue;
    }
    if (discardingOverflow_) {
      continue;
    }
    if (lineLen_ >= kMaxLineLen - 1) {
      Logger::warn("cmd", "line too long (max 95 chars), discarded");
      discardingOverflow_ = true;
      lineLen_ = 0;
      continue;
    }
    line_[lineLen_++] = c;
  }
}

void SerialCommandRouter::printHelp() const {
  io_->print(F("[help] "));
  io_->print(appName_);
  io_->print(F(" commands ("));
  io_->print(commandCount_ + 1);  // +1 for the built-in `help`
  io_->println(F("):"));
  if (droppedCount_ > 0) {
    io_->print(F("  !! "));
    io_->print(droppedCount_);
    io_->print(F(" command(s) DROPPED - table full at "));
    io_->print(kMaxCommands);
    io_->println(F("; raise CROW_SERIAL_MAX_COMMANDS in AppConfig.h"));
  }
  io_->println(F("  help - list commands"));

  // Commands registered with a group print under one heading. Registration
  // order is preserved inside a group; ungrouped commands print flat, which is
  // what every project did before groups existed.
  const char *shown = nullptr;
  for (uint16_t i = 0; i < commandCount_; i++) {
    const char *group = commands_[i].group;
    if (group != nullptr && (shown == nullptr || strcmp(shown, group) != 0)) {
      io_->print(F("  ["));
      io_->print(group);
      io_->println(F("]"));
      shown = group;
    }
    io_->print(F("  "));
    io_->print(commands_[i].name);
    io_->print(F(" - "));
    io_->println(commands_[i].help);
  }
}

void SerialCommandRouter::dispatchLine() {
  String input(line_);
  input.trim();
  if (input.length() == 0) {
    return;
  }

  int space = input.indexOf(' ');
  String command = (space < 0) ? input : input.substring(0, space);
  String args = (space < 0) ? String("") : input.substring(space + 1);
  args.trim();

  if (command == "help") {
    printHelp();
    return;
  }

  for (uint16_t i = 0; i < commandCount_; i++) {
    if (command == commands_[i].name) {
      commands_[i].handler(args);
      return;
    }
  }

  Logger::warn("cmd", "unknown command \"" + command + "\", try: help");
}
