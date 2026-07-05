#include "Logger.h"

void Logger::begin(unsigned long baud) {
  Serial.begin(baud);
  delay(250);
  Serial.println();
  Serial.println(F("[boot] CrowPanel AIoT Arduino Suite"));
}

void Logger::info(const char *scope, const String &message) {
  Serial.print(F("[info] "));
  Serial.print(scope);
  Serial.print(F(" "));
  Serial.println(message);
}

void Logger::warn(const char *scope, const String &message) {
  Serial.print(F("[warn] "));
  Serial.print(scope);
  Serial.print(F(" "));
  Serial.println(message);
}

void Logger::diag(const char *action, const char *status, const String &detail) {
  Serial.print(F("[diag] action="));
  Serial.print(action);
  Serial.print(F(" status="));
  Serial.print(status);
  Serial.print(F(" heap="));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(" detail="));
  Serial.println(detail);
}
