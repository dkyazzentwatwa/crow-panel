#ifndef INKWELL_INK_DOC_H
#define INKWELL_INK_DOC_H

#include <cstdint>
#include <string>
#include <vector>

// The format-neutral document model. TxtParser, MarkdownParser and
// XhtmlParser all emit this; Paginator and the renderer consume only this.
namespace Ink {

enum class BlockType : uint8_t { Body, H1, H2, H3, Quote, Code, ListItem, Rule };

// Render styles the measurer/renderer understand. Headings collapse inline
// styling (a bold word inside an H2 just renders H2).
enum Style : uint8_t {
  kStyleBody = 0, kStyleBold, kStyleItalic, kStyleBoldItalic, kStyleMono,
  kStyleH1, kStyleH2, kStyleH3,
  kStyleCount
};

struct Run {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool mono = false;
};

struct Block {
  BlockType type = BlockType::Body;
  uint8_t listDepth = 0;   // ListItem only (1 = top level)
  bool ordered = false;    // ListItem only
  uint32_t srcOffset = 0;  // byte offset of the block's start in chapter source
  std::vector<Run> runs;
};

inline uint8_t styleFor(const Block &b, const Run &r) {
  switch (b.type) {
    case BlockType::H1: return kStyleH1;
    case BlockType::H2: return kStyleH2;
    case BlockType::H3: return kStyleH3;
    case BlockType::Code: return kStyleMono;
    default: break;
  }
  if (r.mono) return kStyleMono;
  if (r.bold && r.italic) return kStyleBoldItalic;
  if (r.bold) return kStyleBold;
  if (r.italic) return kStyleItalic;
  return kStyleBody;
}

// Concatenated plain text of a block (tests + TOC titles).
inline std::string plainText(const Block &b) {
  std::string out;
  for (const Run &r : b.runs) out += r.text;
  return out;
}

}  // namespace Ink

#endif
