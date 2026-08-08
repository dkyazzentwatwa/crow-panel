#ifndef INKWELL_EPUB_BOOK_H
#define INKWELL_EPUB_BOOK_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Ink {

struct TocEntry {
  std::string title;
  int spineIndex = -1;  // -1 if the href didn't resolve to a spine item
};

// Reads an EPUB from a memory buffer (the whole .epub lives in PSRAM on
// device — 32 MB makes that the simple, robust choice). The buffer must
// outlive the EpubBook.
// Non-copyable and non-movable (declared dtor suppresses moves); hold as
// direct member or unique_ptr.
class EpubBook {
 public:
  EpubBook() = default;
  ~EpubBook() { close(); }
  EpubBook(const EpubBook &) = delete;             // zip_ is a raw mz_zip_archive:
  EpubBook &operator=(const EpubBook &) = delete;  // a copy would double-free m_pState

  bool open(const uint8_t *data, size_t size);  // false = not a usable EPUB
  void close();
  const std::string &title() const { return title_; }
  const std::string &author() const { return author_; }
  size_t chapterCount() const { return spineHrefs_.size(); }
  uint32_t chapterSize(size_t i) const;          // uncompressed entry size; 0 if it
                                                  // exceeds kMaxEntryBytes (unreadable
                                                  // by readEntry) -- for book %
  bool chapterXhtml(size_t i, std::string &out); // extract chapter i
  const std::vector<TocEntry> &toc() const { return toc_; }
  bool coverImage(std::string &out, std::string &mediaType);

 private:
  bool readEntry(const std::string &name, std::string &out);
  void parseOpf(const std::string &opf, const std::string &opfDir);
  void parseNavToc(const std::string &navXhtml);
  void parseNcxToc(const std::string &ncx);
  int spineIndexForHref(const std::string &href) const;
  // pimpl-free: keep the mz_zip_archive in an opaque aligned buffer so the
  // header stays miniz-free (host tests include miniz.h themselves).
  // `mutable` + zero-initialized: chapterSize() is logically const (it
  // doesn't change what the book reports) but miniz's reader calls take a
  // non-const mz_zip_archive*, so the buffer needs mutable rather than a
  // const_cast at every const call site. Zero-initializing means a
  // pre-open() or post-close() inspection never reads uninitialized bytes.
  alignas(8) mutable unsigned char zip_[128] = {};
  bool zipOpen_ = false;
  std::string opfDir_;
  std::string title_, author_;
  std::vector<std::string> spineHrefs_;
  // Resolved href -> spine index, built once when the spine is parsed so
  // spineIndexForHref() (called once per TOC entry) is a lookup instead of
  // a linear scan of the spine per entry.
  std::map<std::string, int> hrefToSpineIndex_;
  std::string coverHref_, coverMedia_;
  std::vector<TocEntry> toc_;
};

}  // namespace Ink

#endif
