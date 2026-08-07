#ifndef INKWELL_INK_BOOK_H
#define INKWELL_INK_BOOK_H

#include "EpubBook.h"
#include "InkDoc.h"

namespace Ink {

enum class Format : uint8_t { Txt, Markdown, Epub };

class InkBook {
 public:
  // data must outlive the InkBook (mock: PROGMEM/static; SD: PSRAM buffer).
  bool open(Format f, const uint8_t *data, size_t size);
  void close();
  Format format() const { return fmt_; }
  const std::string &title() const { return title_; }    // EPUB metadata, else ""
  const std::string &author() const { return author_; }
  size_t chapterCount() const;                            // TXT/MD: 1
  bool loadChapter(size_t i, std::vector<Block> &out);    // parses on demand
  const std::vector<TocEntry> &toc() const { return toc_; }
  // srcOffset of TOC entry i within its chapter (MD headings), 0 for EPUB.
  uint32_t tocOffset(size_t i) const;
  // 0..1000 (permille) position for (chapter, offset) — byte-weighted.
  uint16_t permille(size_t chapter, uint32_t offset) const;

 private:
  Format fmt_ = Format::Txt;
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  EpubBook epub_;
  std::string title_, author_;
  std::vector<TocEntry> toc_;
  std::vector<uint32_t> tocOffsets_;
  std::vector<uint32_t> chapterSizes_;  // for permille
};

}  // namespace Ink

#endif
