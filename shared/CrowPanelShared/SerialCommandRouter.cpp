#include "SerialCommandRouter.h"
#include "Logger.h"

void SerialCommandRouter::begin(Stream &io, const char *appName) {
  io_ = &io;
  appName_ = appName;
  lineLen_ = 0;
  Logger::info("cmd", String(appName_) + " serial commands ready, type: help");
}

bool SerialCommandRouter::on(const char *name, const char *help, SerialCommandHandler handler) {
  if (commandCount_ >= kMaxCommands) {
    Logger::error("cmd", String("command table full, dropped: ") + name);
    return false;
  }
  commands_[commandCount_++] = {name, help, handler};
  return true;
}

void SerialCommandRouter::poll() {
  if (io_ == nullptr) {
    return;
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
  io_->println(F(" commands:"));
  io_->println(F("  help - list commands"));
  for (uint8_t i = 0; i < commandCount_; i++) {
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

  for (uint8_t i = 0; i < commandCount_; i++) {
    if (command == commands_[i].name) {
      commands_[i].handler(args);
      return;
    }
  }

  Logger::warn("cmd", "unknown command \"" + command + "\", try: help");
}
