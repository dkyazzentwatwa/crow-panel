#ifndef INKWELL_LIBRARY_STORE_H
#define INKWELL_LIBRARY_STORE_H

#include <Arduino.h>

#include "InkBook.h"

// Mock backend for Inkwell's library. begin() registers the three embedded
// samples from SampleBooks.h as an in-RAM catalog; Task 9 replaces this
// with an SD-backed store that scans a directory for real files. The
// BookEntry/LibraryStore shape is deliberately kept generic (id/title/
// author/format/bytes/permille, stable-pointer bookData(), a position
// table) so that swap can happen underneath the .ino without changing its
// call sites.
//
// This is the Arduino String / device-side boundary: SampleBooks.h itself
// stays std::string-only so the host test suite could reuse it, but
// LibraryStore is not built by scripts/test-inkwell.sh (Arduino.h isn't
// available on the host).
struct BookEntry {
  String id;        // mock: "sample-txt"; SD later: filename
  String title, author;
  Ink::Format format = Ink::Format::Txt;
  uint32_t bytes = 0;
  uint16_t permille = 0;  // saved progress (mock: in-RAM only, see LibraryStore)
};

class LibraryStore {
 public:
  // Mock registers exactly 3 samples; the ceiling leaves room for a future
  // SD-backed store without another interface change. Fixed-size (no heap
  // growth on a long-running panel), per the repo's storage-policy rule.
  static const size_t kMaxBooks = 8;

  bool begin();  // mock: registers the 3 embedded samples
  size_t count() const { return count_; }
  // Out-of-range i returns a default-constructed BookEntry (empty id/
  // title/author, format Txt, 0 bytes) rather than asserting -- callers
  // that already bounds-check against count() never hit this path.
  const BookEntry &entry(size_t i) const;
  // Stable pointers: SampleBooks.h's samples are static-local, built once
  // and never freed, so data/size stay valid for the process lifetime.
  bool bookData(size_t i, const uint8_t *&data, size_t &size);
  // False (with spine=0, offset=0) when i is out of range OR no position
  // was ever saved for that book -- both cases mean "start of book" to the
  // caller, so openBook() doesn't need to distinguish them.
  bool loadPosition(size_t i, uint16_t &spine, uint32_t &offset);
  bool savePosition(size_t i, uint16_t spine, uint32_t offset, uint16_t permille);

 private:
  struct Position {
    uint16_t spine = 0;
    uint32_t offset = 0;
    bool saved = false;
  };

  // In-RAM position table -- positions do NOT survive a reboot in this
  // mock backend (no SD/flash write path yet). Task 9's SD-backed store is
  // expected to persist this to a small sidecar file per book.
  BookEntry entries_[kMaxBooks];
  Position positions_[kMaxBooks];
  size_t count_ = 0;
};

#endif
