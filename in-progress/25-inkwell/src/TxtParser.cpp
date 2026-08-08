#include "TxtParser.h"

namespace Ink {

std::vector<Block> parseTxt(const std::string &src) {
  std::vector<Block> blocks;
  size_t i = 0, n = src.size();
  while (i < n) {
    while (i < n && (src[i] == '\n' || src[i] == '\r')) ++i;
    if (i >= n) break;
    size_t start = i;
    // "start" must land on the first non-blank character, per the offset
    // contract. Look-ahead only -- must not advance `i`.
    while (start < n && (src[start] == ' ' || src[start] == '\t')) ++start;
    std::string text;
    bool blank = false;
    while (i < n && !blank) {
      size_t eol = src.find('\n', i);
      if (eol == std::string::npos) eol = n;
      size_t end = eol;
      if (end > i && src[end - 1] == '\r') --end;
      std::string line;
      for (size_t k = i; k < end; ++k) {
        char c = (src[k] == '\t' || src[k] == '\r') ? ' ' : src[k];
        if (c == ' ' && !line.empty() && line.back() == ' ') continue;
        line += c;
      }
      while (!line.empty() && line.back() == ' ') line.pop_back();
      size_t first = line.find_first_not_of(' ');
      line = first == std::string::npos ? "" : line.substr(first);
      if (line.empty()) blank = true;
      else {
        if (!text.empty()) text += ' ';
        text += line;
      }
      i = eol == n ? n : eol + 1;
    }
    if (!text.empty()) {
      Block b;
      b.srcOffset = (uint32_t)start;
      b.runs.push_back({text, false, false, false});
      blocks.push_back(std::move(b));
    }
  }
  return blocks;
}

}  // namespace Ink
