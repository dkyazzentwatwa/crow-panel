#ifndef INKWELL_LIBRARY_STORE_H
#define INKWELL_LIBRARY_STORE_H

#include "../config/ProjectConfig.h"

#include <Arduino.h>
#include <vector>

#include "InkBook.h"

// Two backends behind one interface (begin/count/entry/bookData/loadPosition/
// savePosition + sdMounted/backendName), selected at compile time by
// USE_INKWELL_SD -- the .ino never branches on which one is active:
//
//   USE_INKWELL_SD=0 (default): the mock backend. Registers the three
//   embedded samples from SampleBooks.h as an in-RAM catalog; positions live
//   in RAM only and are lost on reboot.
//
//   USE_INKWELL_SD=1 (Task 9): scans /books on the CrowPanel's SD_MMC card
//   for *.txt/*.md/*.epub, caches title/author metadata in
//   /books/.inkwell/catalog.txt so a rescan doesn't have to reopen every
//   EPUB, and persists reading position per book to
//   /books/.inkwell/<name>.pos. See LibraryStore.cpp's SD half for the full
//   on-card layout and the honesty-contract note (compile-verified only --
//   there is no SD card in the dev environment this was written in).
//
// This is the Arduino String / device-side boundary: SampleBooks.h itself
// stays std::string-only so the host test suite could reuse it, but
// LibraryStore is not built by scripts/test-inkwell.sh (Arduino.h isn't
// available on the host).
struct BookEntry {
  String id;        // mock: "sample-txt"; SD: the on-card filename, e.g.
                     // "moby-dick.epub" -- also the key for every SD sidecar
                     // path (.pos, .<chapter>.<hash>.idx, catalog row).
  String title, author;
  Ink::Format format = Ink::Format::Txt;
  uint32_t bytes = 0;
  uint16_t permille = 0;  // saved progress. Mock: RAM only, starts at 0 every
                           // boot. SD: seeded from the book's .pos sidecar at
                           // scan time, then kept live by savePosition().
};

class LibraryStore {
 public:
#if USE_INKWELL_SD
  // Raised from the mock backend's 3-sample ceiling -- a real card can hold
  // far more than 3 books. Each extra slot costs sizeof(BookEntry) in this
  // FIXED table (three Strings + a uint32_t + a uint16_t + an enum byte --
  // ~56 bytes empty on this core, a little more once title/author hold real
  // text): 32 slots is on the order of 2KB even full, negligible next to the
  // ~300KB of internal DRAM this board has to share with everything else.
  static const size_t kMaxBooks = 32;
  // Skip anything on /books bigger than this. PSRAM is 32MB total, and
  // bookData() briefly holds TWO whole-book buffers at once while swapping
  // books -- it only frees the old one once the new one has finished
  // loading (see bookData() below), so peak PSRAM use during a swap is
  // roughly TWO books' worth, not one. 12MB keeps that arithmetic safe with
  // margin: two max-size books is 24MB, leaving 8MB of the 32MB PSRAM
  // budget for the paginator's Line/Page vectors (Paginator.h: "each Line
  // costs roughly ~150 bytes" -- those land in internal DRAM, not PSRAM, but
  // the 8MB margin is deliberately generous rather than cut to the bone) and
  // everything else the app needs. 12MB is still enormous for a single
  // ebook (a multi-hundred-page EPUB with images rarely clears a few MB). A
  // file over this is skipped at scan time, not truncated.
  static const size_t kMaxBookBytes = 12UL * 1024 * 1024;
#else
  // Mock registers exactly 3 samples; fixed-size (no heap growth on a
  // long-running panel), per the repo's storage-policy rule.
  static const size_t kMaxBooks = 8;
#endif

  // Mock: registers the 3 embedded samples, always returns true.
  // SD: mounts SD_MMC (unless another subsystem already has -- see the .cpp),
  // scans /books, and returns whether the card mounted. A false return still
  // leaves the store usable with count()==0 -- the .ino already treats an
  // empty library as "no books", so it keeps running with nothing to read
  // rather than refusing to boot.
  bool begin();
  size_t count() const { return count_; }
  // Out-of-range i returns a default-constructed BookEntry (empty id/
  // title/author, format Txt, 0 bytes) rather than asserting -- callers
  // that already bounds-check against count() never hit this path.
  const BookEntry &entry(size_t i) const;
  // Stable pointers: SampleBooks.h's samples are static-local, built once
  // and never freed, so data/size stay valid for the process lifetime.
  // SD: data points at a PSRAM buffer this store owns and reuses -- see the
  // "single-slot, briefly two during a swap" note on the SD half in
  // LibraryStore.cpp. Valid until the NEXT bookData() call (any index,
  // including the same one again) or releaseBookData().
  bool bookData(size_t i, const uint8_t *&data, size_t &size);
  // Frees the SD backend's whole-book buffer, if one is held, without
  // loading a replacement -- for when the reader goes back to the library
  // view and the pages already turned are the only reason the buffer was
  // still resident. No-op (returns false) for the mock backend or when
  // nothing is currently held. Call after book.close(), not before --
  // InkBook/EpubBook still need the buffer valid until they're closed (see
  // InkBook.h's "data must outlive the InkBook").
  bool releaseBookData();
  // False (with spine=0, offset=0) when i is out of range OR no position
  // was ever saved for that book -- both cases mean "start of book" to the
  // caller, so openBook() doesn't need to distinguish them.
  bool loadPosition(size_t i, uint16_t &spine, uint32_t &offset);
  bool savePosition(size_t i, uint16_t spine, uint32_t offset, uint16_t permille);

  // True once the SD backend has a card mounted with a working /books
  // directory. Always false for the mock backend.
  bool sdMounted() const;
  // "sd" / "mock" -- a status-line label, never a branch condition; the
  // reader pipeline (openBook/paginate) is identical either way.
  const char *backendName() const;

  // Task 9 sidecar hook, called by the .ino right after every layout --
  // both loadChapterAndLayout() (new chapter) and relayoutAndLand() (same
  // chapter, new font/spacing/margin). Writes a page-start-offset index for
  // (book i, chapter) keyed by the paginator's LayoutSettings::hash() if one
  // isn't already on disk, and deletes any sidecar left over from a
  // previous hash for the same book+chapter. No-op (returns false) for the
  // mock backend, an unmounted card, or an out-of-range i. See
  // LibraryStore.cpp's SD half for the v1 scope note: this only WRITES
  // sidecars; nothing reads them back yet beyond the existence check that
  // skips a redundant write.
  bool writePageIndex(size_t i, size_t chapter, uint32_t layoutHash,
                       const std::vector<uint32_t> &pageStarts);

 private:
  BookEntry entries_[kMaxBooks];
  size_t count_ = 0;

#if USE_INKWELL_SD
  bool sdMounted_ = false;
  // Single-slot whole-book buffer, in the sense that only ONE survives
  // between calls -- bookData() frees whatever this pointed at the moment a
  // NEW load succeeds, never before (see the .cpp), so DURING a swap both
  // the old and the new book's bytes are briefly resident at once. That
  // transient overlap is exactly what kMaxBookBytes above is sized against.
  uint8_t *dataBuf_ = nullptr;
  size_t dataBufSize_ = 0;

  void scan();
#else
  struct Position {
    uint16_t spine = 0;
    uint32_t offset = 0;
    bool saved = false;
  };
  // In-RAM position table -- positions do NOT survive a reboot in this mock
  // backend (no SD/flash write path).
  Position positions_[kMaxBooks];
#endif
};

#endif
