#include "DeskTextWrap.h"

void DeskTextWrap::rebuild(const String &text, uint16_t columns) {
  columns_ = columns ? columns : 1;
  length_ = text.length();
  truncated_ = false;
  count_ = 0;
  starts_[count_++] = 0;

  size_t index = 0;
  while (index < length_ && count_ < kMaxLines) {
    // A hard newline always ends the line, however short it is.
    size_t hardBreak = length_;
    for (size_t scan = index; scan < length_; ++scan) {
      if (text[scan] == '\n') {
        hardBreak = scan;
        break;
      }
    }

    if (hardBreak - index <= columns_) {
      if (hardBreak >= length_) break;  // last line, no trailing newline
      index = hardBreak + 1;
      starts_[count_++] = index;
      continue;
    }

    // Too long for one row: break at the last space that fits, so words stay
    // whole. A single word longer than the row is cut at the row edge - the
    // alternative is a line that runs off the panel.
    size_t limit = index + columns_;
    size_t breakAt = limit;
    for (size_t scan = limit; scan > index; --scan) {
      if (text[scan - 1] == ' ') {
        breakAt = scan;
        break;
      }
    }
    index = breakAt;
    starts_[count_++] = index;
  }

  if (count_ >= kMaxLines && index < length_) truncated_ = true;
  if (count_ == 0) count_ = 1;
}

size_t DeskTextWrap::lineStart(uint16_t line) const {
  if (line >= count_) return length_;
  return starts_[line];
}

size_t DeskTextWrap::lineEnd(uint16_t line) const {
  if (line + 1 < count_) {
    const size_t nextStart = starts_[line + 1];
    // Drop the newline that ended this line; a soft wrap has no separator to
    // drop, so only step back when there actually was one.
    return nextStart > starts_[line] ? nextStart : starts_[line];
  }
  return length_;
}

String DeskTextWrap::lineText(const String &text, uint16_t line) const {
  size_t start = lineStart(line);
  size_t end = lineEnd(line);
  if (end > text.length()) end = text.length();
  if (start >= end) return String();
  String result = text.substring(start, end);
  // Trim the hard newline if this line ended on one.
  if (result.length() && result[result.length() - 1] == '\n') {
    result.remove(result.length() - 1);
  }
  return result;
}

uint16_t DeskTextWrap::lineForIndex(size_t index) const {
  if (index >= length_) return count_ ? count_ - 1 : 0;
  // Binary search: this runs on every keystroke and the line table can hold a
  // thousand entries.
  uint16_t low = 0;
  uint16_t high = count_ - 1;
  while (low < high) {
    const uint16_t mid = static_cast<uint16_t>((low + high + 1) / 2);
    if (starts_[mid] <= index) low = mid;
    else high = static_cast<uint16_t>(mid - 1);
  }
  return low;
}

uint16_t DeskTextWrap::columnForIndex(size_t index) const {
  const uint16_t line = lineForIndex(index);
  const size_t start = starts_[line];
  return index >= start ? static_cast<uint16_t>(index - start) : 0;
}

size_t DeskTextWrap::indexFor(uint16_t line, uint16_t column) const {
  if (count_ == 0) return 0;
  if (line >= count_) line = static_cast<uint16_t>(count_ - 1);
  const size_t start = starts_[line];
  size_t end = lineEnd(line);
  // Land at the end of the line rather than wrapping onto the next one when a
  // tap falls past the last character.
  if (end > start && end <= length_ && end > 0) {
    // lineEnd() may point at the newline; clamp the caret before it.
    while (end > start && end - start > columns_) --end;
  }
  const size_t target = start + column;
  return target > end ? end : target;
}
