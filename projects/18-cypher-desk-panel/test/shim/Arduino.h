// Minimal Arduino shim for the Cypher Desk host tests.
//
// Only what DeskWavReader.cpp, DeskAviReader.cpp and DeskTextWrap.cpp actually
// touch. Those three are deliberately free of SD_MMC and display headers - the
// byte-source abstractions exist precisely so the exact translation units that
// ship in the firmware also build with a plain g++ here. Nothing in this file
// ships.
#ifndef CYPHER_DESK_HOST_ARDUINO_H
#define CYPHER_DESK_HOST_ARDUINO_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) { set("%d", static_cast<long>(v)); }
  String(unsigned int v) { set("%lu", static_cast<unsigned long>(v)); }
  String(long v) { set("%ld", v); }
  String(unsigned long v) { set("%lu", v); }
  String(unsigned long long v) { set("%llu", v); }
  String(long long v) { set("%lld", v); }

  String &operator+=(const String &o) {
    s_ += o.s_;
    return *this;
  }
  friend String operator+(const String &a, const String &b) {
    String out(a);
    out += b;
    return out;
  }

  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  char operator[](size_t i) const { return i < s_.size() ? s_[i] : '\0'; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const char *o) const { return !(*this == o); }

  String substring(size_t from) const {
    return from >= s_.size() ? String() : String(s_.substr(from));
  }
  String substring(size_t from, size_t to) const {
    if (from >= s_.size() || to <= from) return String();
    return String(s_.substr(from, std::min(to, s_.size()) - from));
  }
  void remove(size_t index) {
    if (index < s_.size()) s_.erase(index);
  }
  void remove(size_t index, size_t count) {
    if (index < s_.size()) s_.erase(index, count);
  }
  void setCharAt(size_t index, char c) {
    if (index < s_.size()) s_[index] = c;
  }
  int indexOf(const String &needle, size_t from = 0) const {
    const size_t at = s_.find(needle.s_, from);
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  int lastIndexOf(char c) const {
    const size_t at = s_.rfind(c);
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  bool startsWith(const String &p) const { return s_.rfind(p.s_, 0) == 0; }
  bool endsWith(const String &p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  bool equalsIgnoreCase(const String &o) const {
    String a(*this), b(o);
    a.toLowerCase();
    b.toLowerCase();
    return a.s_ == b.s_;
  }
  void toLowerCase() {
    std::transform(s_.begin(), s_.end(), s_.begin(), [](unsigned char c) { return ::tolower(c); });
  }
  void trim() {
    const size_t first = s_.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      s_.clear();
      return;
    }
    s_ = s_.substr(first, s_.find_last_not_of(" \t\r\n") - first + 1);
  }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }

 private:
  template <typename T>
  void set(const char *spec, T v) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), spec, v);
    s_ = buffer;
  }
  std::string s_;
};

inline uint32_t millis() { return 0; }

#endif
