#pragma once

// Stub of cypher-chat's OutputManager. The vendored MeshManager logs through a
// global `output` object; here every method is a no-op template so any
// argument types compile, and the mesh core builds without dragging in the
// display / BLE terminal. (Forward to Serial in OutputManager.cpp if you want
// the bridge's mesh logs on USB.)

#include <Arduino.h>
#include <stdarg.h>

class OutputManager {
 public:
  void begin() {}
  template <typename T> void print(const T &) {}
  template <typename T, typename U> void print(const T &, U) {}
  template <typename T> void println(const T &) {}
  template <typename T, typename U> void println(const T &, U) {}
  void println() {}
  void printf(const char *, ...) {}
  void setUSBEnabled(bool) {}
  void setBTEnabled(bool) {}
  bool isUSBEnabled() const { return true; }
  bool isBTEnabled() const { return false; }
};

extern OutputManager output;
