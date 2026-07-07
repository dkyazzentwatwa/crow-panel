#ifndef CROW_PANEL_LOGGER_H
#define CROW_PANEL_LOGGER_H

#include <Arduino.h>

class Logger {
 public:
  static void begin(unsigned long baud = 115200);
  static void info(const char *scope, const String &message);
  static void warn(const char *scope, const String &message);
  static void error(const char *scope, const String &message);
  static void diag(const char *action, const char *status, const String &detail);
};

#endif
