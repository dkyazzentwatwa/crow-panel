#ifndef CYPHER_DESK_TEXT_WRAP_H
#define CYPHER_DESK_TEXT_WRAP_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Soft word wrap for the editor.
//
// The editor used to render logical lines and scroll HORIZONTALLY: a long
// paragraph ran off the right edge and you had to walk the cursor sideways to
// read it, on a device whose entire purpose is writing prose. This maps the
// buffer to display lines instead, breaking at spaces where it can.
//
// Deliberately character-based rather than pixel-based. The editor advances
// the cursor bar by a fixed 11 px per column, so the layout and the cursor
// have to agree on columns being uniform; measuring proportional widths here
// would put the caret in the wrong place.
//
// No display headers, so this compiles headless and is exercised by the host
// tests.
class DeskTextWrap {
 public:
  // 1024 lines at 78 columns covers a 32,000-character document with room to
  // spare; the flag says so if a document ever exceeds it.
  static constexpr uint16_t kMaxLines = 1024;

  void rebuild(const String &text, uint16_t columns);
  uint16_t lineCount() const { return count_; }
  uint16_t columns() const { return columns_; }
  bool truncated() const { return truncated_; }

  size_t lineStart(uint16_t line) const;
  // Exclusive, and excludes the newline that ended the line (if any).
  size_t lineEnd(uint16_t line) const;
  String lineText(const String &text, uint16_t line) const;

  uint16_t lineForIndex(size_t index) const;
  uint16_t columnForIndex(size_t index) const;
  size_t indexFor(uint16_t line, uint16_t column) const;

 private:
  uint32_t starts_[kMaxLines] = {};
  uint16_t count_ = 1;
  uint16_t columns_ = 78;
  uint32_t length_ = 0;
  bool truncated_ = false;
};

#endif
