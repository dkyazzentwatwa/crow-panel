#ifndef INKWELL_INK_BOOK_H
#define INKWELL_INK_BOOK_H

#include "EpubBook.h"
#include "InkDoc.h"

namespace Ink {

enum class Format : uint8_t { Txt, Markdown, Epub };

// Non-copyable and non-movable: it holds an EpubBook member, and EpubBook
// itself is non-copyable/non-movable (a copy of its raw mz_zip_archive
// would double-free m_pState). No dtor/copy/move is declared here, but
// the implicit copy/move constructors and assignment operators are
// already implicitly deleted as a result of the EpubBook member alone --
// documented so a future edit doesn't "fix" that by adding a hand-rolled
// copy path.
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
  // False for i out of range, or (EPUB only) a TOC entry whose href never
  // resolved to a spine item -- the UI should grey those rows rather than
  // jump nowhere. True with chapter=0 for any valid MD TOC entry (MD's
  // TOC is always the single chapter 0). TXT never has TOC entries.
  bool tocTarget(size_t i, size_t &chapter) const;
  // 0..1000 (permille) position for (chapter, offset) — byte-weighted.
  uint16_t permille(size_t chapter, uint32_t offset) const;
  // EPUB only -- false for TXT/MD (out/mediaType left untouched), so a
  // caller wanting a cover thumbnail never needs a second, separate
  // EpubBook open path of its own.
  bool coverImage(std::string &out, std::string &mediaType);

 private:
  Format fmt_ = Format::Txt;
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  EpubBook epub_;
  std::string title_, author_;
  std::vector<TocEntry> toc_;
  std::vector<uint32_t> tocOffsets_;
  std::vector<uint32_t> chapterSizes_;  // for permille
  uint64_t totalSize_ = 0;              // sum(chapterSizes_), cached at open()
                                         // since the HUD scrubber calls
                                         // permille() once per drag sample
};

}  // namespace Ink

#endif
