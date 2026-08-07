#ifndef INKWELL_EPUB_BOOK_H
#define INKWELL_EPUB_BOOK_H

#include <cstdint>
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
class EpubBook {
 public:
  bool open(const uint8_t *data, size_t size);  // false = not a usable EPUB
  void close();
  const std::string &title() const { return title_; }
  const std::string &author() const { return author_; }
  size_t chapterCount() const { return spineHrefs_.size(); }
  uint32_t chapterSize(size_t i) const;          // compressed entry size (for book %)
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
  alignas(8) unsigned char zip_[128];
  bool zipOpen_ = false;
  std::string opfDir_;
  std::string title_, author_;
  std::vector<std::string> spineHrefs_;
  std::string coverHref_, coverMedia_;
  std::vector<TocEntry> toc_;
};

}  // namespace Ink

#endif
