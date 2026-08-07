// The InkBook facade: the ONLY file that knows about all three formats
// (TxtParser, MarkdownParser, XhtmlParser/EpubBook). Everything above this
// layer (Paginator, the renderer, the serial/mock UI) talks to one
// open-book API and never branches on format itself.
#include "InkBook.h"

#include "MarkdownParser.h"
#include "TxtParser.h"
#include "XhtmlParser.h"

namespace Ink {

namespace {
// data/size may be null/0 for an unopened book, or a genuinely empty
// TXT/MD source (open() always succeeds for those, even empty per spec).
// std::string(ptr, 0) with a null ptr is technically UB even though count
// is zero, so guard it explicitly rather than relying on implementation
// behavior.
std::string bufferToString(const uint8_t *data, size_t size) {
  if (size == 0) return std::string();
  return std::string(reinterpret_cast<const char *>(data), size);
}
}  // namespace

bool InkBook::open(Format f, const uint8_t *data, size_t size) {
  // Re-opening a live InkBook must fully tear down whatever it already
  // holds first (title/toc/chapterSizes_ and epub_ itself) -- callers may
  // open() again without calling close() themselves.
  close();

  switch (f) {
    case Format::Txt:
      fmt_ = f;
      data_ = data;
      size_ = size;
      chapterSizes_ = {(uint32_t)size};
      return true;  // TXT always succeeds, even for an empty buffer

    case Format::Markdown: {
      fmt_ = f;
      data_ = data;
      size_ = size;
      chapterSizes_ = {(uint32_t)size};
      // One pre-scan of the whole document so the TOC is ready the instant
      // open() returns -- the mock/serial layer needs the TOC immediately,
      // and this is the only parse open() performs. loadChapter() parses
      // again on demand and InkBook caches no blocks itself (see
      // loadChapter() below), so this is O(doc) exactly once per book
      // open, not a hidden per-access cost.
      std::vector<Block> blocks = parseMarkdown(bufferToString(data, size));
      for (const Block &b : blocks) {
        if (b.type == BlockType::H1 || b.type == BlockType::H2) {
          TocEntry e;
          e.title = plainText(b);
          e.spineIndex = 0;
          toc_.push_back(e);
          tocOffsets_.push_back(b.srcOffset);
        }
      }
      return true;  // MD always succeeds, even with zero headings
    }

    case Format::Epub: {
      // epub_.open() calls its own close() on every failure path, so a
      // failed attempt here already leaves epub_ (and this InkBook, whose
      // own fields were reset by the close() above and are untouched
      // below) in a clean, reusable state.
      if (!epub_.open(data, size)) return false;
      fmt_ = f;
      data_ = data;
      size_ = size;
      title_ = epub_.title();
      author_ = epub_.author();
      toc_ = epub_.toc();
      tocOffsets_.assign(toc_.size(), 0);  // EPUB TOC offsets are always 0
      chapterSizes_.resize(epub_.chapterCount());
      for (size_t i = 0; i < chapterSizes_.size(); ++i) {
        // Compressed entry size, not decompressed/rendered length -- so
        // permille() is compressed-byte-weighted for EPUB. An accepted
        // approximation: an image-heavy chapter's own images live in
        // separate zip entries, not counted here, but a text-heavy
        // chapter that happens to compress unusually well or poorly will
        // skew slightly. Good enough for a progress bar, not exact.
        chapterSizes_[i] = epub_.chapterSize(i);
      }
      return true;
    }
  }
  return false;  // unreachable for a valid Format value
}

void InkBook::close() {
  epub_.close();
  fmt_ = Format::Txt;
  data_ = nullptr;
  size_ = 0;
  title_.clear();
  author_.clear();
  toc_.clear();
  tocOffsets_.clear();
  chapterSizes_.clear();
}

size_t InkBook::chapterCount() const {
  if (fmt_ == Format::Epub) return epub_.chapterCount();
  return 1;  // TXT/MD: the whole buffer is one chapter
}

bool InkBook::loadChapter(size_t i, std::vector<Block> &out) {
  // No caching here by design: every call re-parses from data_/size_ (or
  // re-extracts+re-parses the EPUB chapter). The caller (Paginator's
  // sidecar cache, or the mock UI) owns caching parsed blocks/pages;
  // InkBook stays a stateless-per-call translator from bytes to Blocks.
  switch (fmt_) {
    case Format::Txt:
      if (i != 0) return false;
      out = parseTxt(bufferToString(data_, size_));
      return true;

    case Format::Markdown:
      if (i != 0) return false;
      out = parseMarkdown(bufferToString(data_, size_));
      return true;

    case Format::Epub: {
      std::string xhtml;
      if (!epub_.chapterXhtml(i, xhtml)) return false;
      out = parseXhtml(xhtml);
      return true;
    }
  }
  return false;  // unreachable for a valid Format value
}

uint32_t InkBook::tocOffset(size_t i) const {
  if (i >= tocOffsets_.size()) return 0;
  return tocOffsets_[i];
}

uint16_t InkBook::permille(size_t chapter, uint32_t offset) const {
  if (chapterSizes_.empty()) return 0;
  if (chapter >= chapterSizes_.size()) chapter = chapterSizes_.size() - 1;

  // uint64_t throughout: a 4GB book times 1000 would overflow uint32_t --
  // silly for anything that fits in 32MB PSRAM, but free to avoid.
  uint64_t total = 0;
  for (uint32_t sz : chapterSizes_) total += sz;
  if (total == 0) return 0;

  uint64_t before = 0;
  for (size_t i = 0; i < chapter; ++i) before += chapterSizes_[i];

  uint64_t capped = offset;
  if (capped > chapterSizes_[chapter]) capped = chapterSizes_[chapter];

  return (uint16_t)(((before + capped) * 1000ull) / total);
}

}  // namespace Ink
