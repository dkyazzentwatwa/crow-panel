// Minimal Arduino shim for the Cypher Keys host tests.
//
// Only what src/HidKeyboard.cpp and src/KeysTouch.cpp actually touch: the
// integer typedefs, a tiny String, and a millis() the test drives by hand so
// poll cadence and release debounce are deterministic. Nothing here ships.
#ifndef CYPHER_KEYS_HOST_ARDUINO_H
#define CYPHER_KEYS_HOST_ARDUINO_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

// Test-controlled clock. host_main.cpp owns the storage and steps it.
extern uint32_t gHostMillis;
inline uint32_t millis() { return gHostMillis; }

// Just enough of Arduino's String for diagnostics() to build.
class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  explicit String(int v) { set("%d", (long)v); }
  explicit String(unsigned int v) { set("%lu", (unsigned long)v); }
  explicit String(long v) { set("%ld", v); }
  explicit String(unsigned long v) { set("%lu", v); }

  String &operator+=(const String &o) {
    s_ += o.s_;
    return *this;
  }
  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }

  friend String operator+(const String &a, const String &b) {
    String out(a);
    out += b;
    return out;
  }

 private:
  void set(const char *spec, long v) {
    char buf[24];
    snprintf(buf, sizeof(buf), spec, v);
    s_ = buf;
  }
  void set(const char *spec, unsigned long v) {
    char buf[24];
    snprintf(buf, sizeof(buf), spec, v);
    s_ = buf;
  }
  std::string s_;
};

#endif
