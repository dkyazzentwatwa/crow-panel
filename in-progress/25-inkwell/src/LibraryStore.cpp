#include "LibraryStore.h"

#if USE_INKWELL_SD
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <CrowPanelShared.h>
#else
#include "SampleBooks.h"
#endif

namespace {
// Returned by entry() for an out-of-range index -- see the header comment.
const BookEntry kEmptyEntry;

#if USE_INKWELL_SD
// True if `name` ends with `ext` (dot included), case-insensitively.
bool hasExtensionCI(const String &name, const char *ext) {
  size_t extLen = strlen(ext);
  if (name.length() < extLen) return false;
  String tail = name.substring(name.length() - extLen);
  tail.toLowerCase();
  String lowerExt(ext);
  lowerExt.toLowerCase();
  return tail == lowerExt;
}

// false (format left at its default) for any extension this store doesn't
// recognize -- the caller skips the file entirely rather than guessing.
Ink::Format formatForName(const String &name, bool &recognized) {
  recognized = true;
  if (hasExtensionCI(name, ".txt")) return Ink::Format::Txt;
  if (hasExtensionCI(name, ".md")) return Ink::Format::Markdown;
  if (hasExtensionCI(name, ".epub")) return Ink::Format::Epub;
  recognized = false;
  return Ink::Format::Txt;
}

// "moby-dick.epub" -> "moby-dick" -- TXT/MD title fallback (and EPUB's own
// fallback if metadata extraction fails).
String stemOf(const String &name) {
  int dot = name.lastIndexOf('.');
  return dot > 0 ? name.substring(0, dot) : name;
}

// Catalog/position/sidecar text is line- and '|'-structured; scrub the three
// characters that would corrupt that structure out of free-text metadata
// (EPUB title/author). FAT/exFAT filenames can never contain '|' themselves
// (it's an illegal character on both filesystems), so entries_[i].id never
// needs this treatment -- only title/author, which come from arbitrary
// EPUB OPF metadata, do.
String catalogSafe(String s) {
  s.replace('|', '/');
  s.replace('\n', ' ');
  s.replace('\r', ' ');
  return s;
}

String bookPath(const String &name) { return String(INKWELL_BOOKS_DIR) + "/" + name; }
String posPath(const String &name) { return String(INKWELL_CATALOG_DIR) + "/" + name + ".pos"; }
String pageIndexPath(const String &name, size_t chapter, uint32_t layoutHash) {
  return String(INKWELL_CATALOG_DIR) + "/" + name + "." + String((unsigned)chapter) + "." +
         String(layoutHash) + ".idx";
}

bool ensureDir(const String &path) { return SD_MMC.exists(path) || SD_MMC.mkdir(path); }

// Reads a whole small text file into a String; "" if missing/unreadable.
// Only ever used for the catalog and the few-dozen-byte .pos/.idx sidecars --
// a book BODY always goes through bookData()'s PSRAM path instead, never
// through Strings.
String readSmallFile(const String &path, size_t maxLen) {
  if (!SD_MMC.exists(path)) return String();
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return String();
  String body;
  body.reserve(maxLen < 4096 ? maxLen : 4096);
  while (f.available() && body.length() < maxLen) body += (char)f.read();
  f.close();
  return body;
}

// Overwrites `path` with `body`, removing any existing file first --
// mirrors Cypher Desk's (project 18) DeskStorage convention of never
// trusting FILE_WRITE alone to truncate. Not transactional: a power-loss
// mid-write can leave a torn catalog/position/sidecar file, but every one of
// these is a cache or a resume hint that the next scan/open self-heals (a
// torn catalog line just gets re-derived by reopening that one EPUB; a torn
// .pos file fails the tolerant parser below and falls back to position 0)
// -- not worth DeskStorage's full temp+rename+backup machinery here.
bool writeSmallFile(const String &path, const String &body) {
  SD_MMC.remove(path);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  size_t written = f.print(body);
  f.close();
  return written == body.length();
}

struct CatalogRow {
  String name, title, author;
  uint32_t size = 0;
};

// Parses catalog.txt's "name|size|title|author" lines. A malformed line
// (wrong field count, non-numeric size) is dropped silently -- scan() just
// re-derives that one file's metadata as if it had never been cached.
std::vector<CatalogRow> loadCatalogRows() {
  std::vector<CatalogRow> rows;
  String body = readSmallFile(INKWELL_CATALOG_PATH, 64000);
  int start = 0;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    start = end + 1;
    line.trim();
    if (line.length() == 0) continue;

    int p1 = line.indexOf('|');
    int p2 = (p1 < 0) ? -1 : line.indexOf('|', p1 + 1);
    int p3 = (p2 < 0) ? -1 : line.indexOf('|', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) continue;

    String sizeStr = line.substring(p1 + 1, p2);
    bool sizeOk = sizeStr.length() > 0;
    for (size_t i = 0; sizeOk && i < sizeStr.length(); ++i) {
      if (!isDigit((unsigned char)sizeStr[i])) sizeOk = false;
    }
    if (!sizeOk) continue;

    CatalogRow row;
    row.name = line.substring(0, p1);
    row.size = (uint32_t)strtoul(sizeStr.c_str(), nullptr, 10);
    row.title = line.substring(p2 + 1, p3);
    row.author = line.substring(p3 + 1);
    rows.push_back(row);
  }
  return rows;
}

const CatalogRow *findCatalogRow(const std::vector<CatalogRow> &rows, const String &name,
                                  uint32_t size) {
  for (const CatalogRow &row : rows) {
    if (row.name == name && row.size == size) return &row;
  }
  return nullptr;
}

// Opens just long enough to read title/author, then frees the buffer --
// scan()'s only reason to touch EpubBook/miniz. This buffer is entirely
// separate from bookData()'s single-slot buffer: a metadata-only scan pass
// never holds more than this one book's bytes, and never touches the slot a
// caller may currently have open for reading.
bool readEpubMetadata(const String &path, size_t size, String &title, String &author) {
  uint8_t *buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (buf == nullptr) return false;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    heap_caps_free(buf);
    return false;
  }
  size_t got = f.read(buf, size);
  f.close();
  bool ok = false;
  if (got == size) {
    Ink::InkBook book;
    if (book.open(Ink::Format::Epub, buf, size)) {
      title = book.title().c_str();
      author = book.author().c_str();
      book.close();
      ok = true;
    }
  }
  heap_caps_free(buf);
  return ok;
}

// Tolerant "spine=N off=N pct=N" parser shared by loadPosition() and scan()'s
// permille seed. A key whose value isn't all-digits is ignored (its output
// field keeps whatever the caller passed in); sawSpine/sawOffset tell the
// caller whether the two fields loadPosition() actually needs were both
// present and numeric.
void parsePositionLine(const String &body, uint16_t &spine, uint32_t &offset,
                        uint16_t &permille, bool &sawSpine, bool &sawOffset) {
  sawSpine = false;
  sawOffset = false;
  int i = 0;
  int len = (int)body.length();
  while (i < len) {
    while (i < len && isSpace((unsigned char)body[i])) ++i;
    int start = i;
    while (i < len && !isSpace((unsigned char)body[i])) ++i;
    if (i == start) continue;
    String token = body.substring(start, i);
    int eq = token.indexOf('=');
    if (eq <= 0) continue;
    String key = token.substring(0, eq);
    String value = token.substring(eq + 1);
    bool numeric = value.length() > 0;
    for (size_t k = 0; numeric && k < value.length(); ++k) {
      if (!isDigit((unsigned char)value[k])) numeric = false;
    }
    if (!numeric) continue;
    unsigned long v = strtoul(value.c_str(), nullptr, 10);
    if (key == "spine") {
      spine = (uint16_t)v;
      sawSpine = true;
    } else if (key == "off") {
      offset = (uint32_t)v;
      sawOffset = true;
    } else if (key == "pct") {
      permille = (uint16_t)v;
    }
  }
}
#endif  // USE_INKWELL_SD
}  // namespace

#if USE_INKWELL_SD

bool LibraryStore::begin() {
  count_ = 0;
  sdMounted_ = false;

  // Don't re-mount a card another subsystem already brought up -- same guard
  // project 18 (Cypher Desk) and project 22 (Cypher Boy) use. Inkwell is the
  // only SD consumer in this project, so it owns SD_MMC.begin() outright;
  // there is no separate mount-owner service to hand this off to.
  bool alreadyMounted = SD_MMC.cardType() != CARD_NONE;
  if (!alreadyMounted && !SD_MMC.begin("/sdcard", INKWELL_SDMMC_1BIT != 0)) {
    Logger::warn("inkwell", "SD_MMC mount failed; library empty (see `books`)");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Logger::warn("inkwell", "no SD card detected; library empty (see `books`)");
    return false;
  }

  if (!ensureDir(INKWELL_BOOKS_DIR)) {
    Logger::error("inkwell", "could not create " INKWELL_BOOKS_DIR " on the card");
    return false;
  }
  if (!ensureDir(INKWELL_CATALOG_DIR)) {
    Logger::error("inkwell", "could not create " INKWELL_CATALOG_DIR " on the card");
    return false;
  }

  sdMounted_ = true;
  scan();
  return true;
}

void LibraryStore::scan() {
  count_ = 0;
  std::vector<CatalogRow> cached = loadCatalogRows();
  std::vector<CatalogRow> fresh;  // rebuilt from exactly what's kept this pass

  File dir = SD_MMC.open(INKWELL_BOOKS_DIR);
  if (!dir || !dir.isDirectory()) {
    Logger::error("inkwell", "mounted, but cannot open " INKWELL_BOOKS_DIR " on the card");
    return;
  }

  for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
    bool isDir = file.isDirectory();
    String fullName = String(file.name());
    uint32_t size = isDir ? 0 : (uint32_t)file.size();
    file.close();

    // File::name() is documented to return the basename, but strip
    // defensively anyway (same defensive strip project 22's GbRomStore
    // uses) in case a core version hands back a full path instead.
    int slash = fullName.lastIndexOf('/');
    String name = (slash >= 0) ? fullName.substring(slash + 1) : fullName;

    if (isDir) continue;                 // ".inkwell" itself, or any subfolder
    if (name.startsWith(".")) continue;  // dotfiles, incl. macOS "._" AppleDoubles
    bool recognized = false;
    Ink::Format format = formatForName(name, recognized);
    if (!recognized) continue;
    if (size == 0 || size > kMaxBookBytes) continue;
    if (count_ >= kMaxBooks) {
      Logger::error("inkwell", "more than kMaxBooks books on /books; extras ignored");
      break;
    }

    String title, author;
    const CatalogRow *cachedRow = findCatalogRow(cached, name, size);
    if (cachedRow != nullptr) {
      // Exact name+size match: trust the cache, never reopen the EPUB.
      title = cachedRow->title;
      author = cachedRow->author;
    } else if (format == Ink::Format::Epub) {
      if (!readEpubMetadata(bookPath(name), size, title, author)) {
        title = stemOf(name);  // unreadable/corrupt EPUB: fall back like TXT/MD
        author = "";
      }
    } else {
      title = stemOf(name);
      author = "";
    }

    BookEntry &e = entries_[count_];
    e.id = name;
    e.title = title;
    e.author = author;
    e.format = format;
    e.bytes = size;
    e.permille = 0;
    // Seed from the .pos sidecar (if any) so `books` shows real progress
    // without requiring the book be reopened this session.
    String posBody = readSmallFile(posPath(name), 256);
    if (posBody.length() > 0) {
      uint16_t spine = 0, permille = 0;
      uint32_t offset = 0;
      bool sawSpine = false, sawOffset = false;
      parsePositionLine(posBody, spine, offset, permille, sawSpine, sawOffset);
      e.permille = permille;
    }
    ++count_;

    CatalogRow row;
    row.name = name;
    row.size = size;
    row.title = title;
    row.author = author;
    fresh.push_back(row);
  }
  dir.close();

  // Rewrite the catalog from exactly what's registered this pass -- drops
  // stale lines for files removed or renamed since the last scan, so the
  // cache never grows without bound.
  String body;
  for (const CatalogRow &row : fresh) {
    body += row.name + "|" + String(row.size) + "|" + catalogSafe(row.title) + "|" +
            catalogSafe(row.author) + "\n";
  }
  writeSmallFile(INKWELL_CATALOG_PATH, body);
}

const BookEntry &LibraryStore::entry(size_t i) const {
  if (i >= count_) return kEmptyEntry;
  return entries_[i];
}

bool LibraryStore::bookData(size_t i, const uint8_t *&data, size_t &size) {
  if (i >= count_ || !sdMounted_) return false;

  File f = SD_MMC.open(bookPath(entries_[i].id), FILE_READ);
  if (!f || f.isDirectory()) return false;
  size_t fileSize = f.size();
  if (fileSize == 0 || fileSize > kMaxBookBytes) {
    f.close();
    return false;
  }
  uint8_t *buf = (uint8_t *)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM);
  if (buf == nullptr) {
    f.close();
    return false;
  }
  size_t got = f.read(buf, fileSize);
  f.close();
  if (got != fileSize) {
    heap_caps_free(buf);
    return false;
  }

  // Single-slot policy: free the PREVIOUS whole-book buffer only now that the
  // new one has loaded successfully -- never before. A failed load above
  // (short read, alloc failure, open failure) leaves whatever was already
  // resident untouched, so a bad SD read can never take down a book that was
  // already open. The .ino satisfies InkBook's own "data must outlive the
  // InkBook" contract (InkBook.h) on top of this by calling book.close()
  // before it ever calls back in here for a different book -- see openBook()
  // in 25-inkwell.ino.
  if (dataBuf_ != nullptr) heap_caps_free(dataBuf_);
  dataBuf_ = buf;
  dataBufSize_ = fileSize;
  data = dataBuf_;
  size = dataBufSize_;
  return true;
}

bool LibraryStore::loadPosition(size_t i, uint16_t &spine, uint32_t &offset) {
  spine = 0;
  offset = 0;
  if (i >= count_ || !sdMounted_) return false;
  String body = readSmallFile(posPath(entries_[i].id), 256);
  if (body.length() == 0) return false;
  uint16_t permille = 0;
  bool sawSpine = false, sawOffset = false;
  parsePositionLine(body, spine, offset, permille, sawSpine, sawOffset);
  if (!sawSpine || !sawOffset) {
    spine = 0;
    offset = 0;
    return false;
  }
  return true;
}

bool LibraryStore::savePosition(size_t i, uint16_t spine, uint32_t offset, uint16_t permille) {
  if (i >= count_ || !sdMounted_) return false;
  entries_[i].permille = permille;
  // Written every call, no batching: page turns are seconds apart (a human
  // reading, not a tight loop), so there's no write storm to coalesce, and
  // always-write means there's never a "did that last turn actually make it
  // to the card before power-loss" question to answer.
  String body = "spine=" + String(spine) + " off=" + String(offset) + " pct=" +
                String(permille) + "\n";
  return writeSmallFile(posPath(entries_[i].id), body);
}

bool LibraryStore::sdMounted() const { return sdMounted_; }
const char *LibraryStore::backendName() const { return "sd"; }

bool LibraryStore::writePageIndex(size_t i, size_t chapter, uint32_t layoutHash,
                                   const std::vector<uint32_t> &pageStarts) {
  if (i >= count_ || !sdMounted_) return false;
  const String &name = entries_[i].id;
  String path = pageIndexPath(name, chapter, layoutHash);
  if (SD_MMC.exists(path)) return true;  // already current for this layout

  // Stale-hash cleanup: drop any other <name>.<chapter>.*.idx before writing
  // the new one, so a font/spacing/margin change doesn't leave the previous
  // layout's sidecar on the card forever. v1 scope: WRITE only -- nothing
  // reads these back beyond the SD_MMC.exists() check just above (see
  // LibraryStore.h's writePageIndex doc comment).
  String prefix = name + "." + String((unsigned)chapter) + ".";
  File dir = SD_MMC.open(INKWELL_CATALOG_DIR);
  if (dir && dir.isDirectory()) {
    for (File entryFile = dir.openNextFile(); entryFile; entryFile = dir.openNextFile()) {
      bool isDir = entryFile.isDirectory();
      String fullName = String(entryFile.name());
      entryFile.close();
      if (isDir) continue;
      int slash = fullName.lastIndexOf('/');
      String entryName = (slash >= 0) ? fullName.substring(slash + 1) : fullName;
      if (entryName.startsWith(prefix) && entryName.endsWith(".idx")) {
        SD_MMC.remove(String(INKWELL_CATALOG_DIR) + "/" + entryName);
      }
    }
    dir.close();
  }

  String body;
  for (uint32_t off : pageStarts) body += String(off) + "\n";
  return writeSmallFile(path, body);
}

#else  // !USE_INKWELL_SD -- mock backend

bool LibraryStore::begin() {
  count_ = 0;

  // Each sample is skipped (not registered) if it comes back empty --
  // guards against SampleBooks.h's buildSampleEpubZip() ever returning its
  // documented empty-string failure case (a miniz writer error). TXT/MD
  // are static compiled-in literals that can never legitimately be empty,
  // but the same guard costs nothing and keeps all three paths uniform.
  if (!Ink::sampleTxt().empty()) {
    entries_[count_].id = "sample-txt";
    entries_[count_].title = "The Lighthouse Keeper's Ledger";
    entries_[count_].author = "Anonymous";
    entries_[count_].format = Ink::Format::Txt;
    entries_[count_].bytes = (uint32_t)Ink::sampleTxt().size();
    entries_[count_].permille = 0;
    ++count_;
  }

  if (!Ink::sampleMarkdown().empty()) {
    entries_[count_].id = "sample-md";
    entries_[count_].title = "Inkwell Format Demo";
    entries_[count_].author = "Project 25";
    entries_[count_].format = Ink::Format::Markdown;
    entries_[count_].bytes = (uint32_t)Ink::sampleMarkdown().size();
    entries_[count_].permille = 0;
    ++count_;
  }

  if (!Ink::sampleEpub().empty()) {
    entries_[count_].id = "sample-epub";
    entries_[count_].title = "The Inkwell Sampler";
    entries_[count_].author = "Project 25";
    entries_[count_].format = Ink::Format::Epub;
    entries_[count_].bytes = (uint32_t)Ink::sampleEpub().size();
    entries_[count_].permille = 0;
    ++count_;
  }

  for (size_t i = 0; i < kMaxBooks; ++i) positions_[i] = Position();
  return true;
}

const BookEntry &LibraryStore::entry(size_t i) const {
  if (i >= count_) return kEmptyEntry;
  return entries_[i];
}

bool LibraryStore::bookData(size_t i, const uint8_t *&data, size_t &size) {
  if (i >= count_) return false;
  // mock-only: dispatch works because each sample has a unique format;
  // the SD backend (USE_INKWELL_SD=1, see above) replaces this wholesale.
  switch (entries_[i].format) {
    case Ink::Format::Txt: {
      const std::string &s = Ink::sampleTxt();
      data = reinterpret_cast<const uint8_t *>(s.data());
      size = s.size();
      return true;
    }
    case Ink::Format::Markdown: {
      const std::string &s = Ink::sampleMarkdown();
      data = reinterpret_cast<const uint8_t *>(s.data());
      size = s.size();
      return true;
    }
    case Ink::Format::Epub: {
      const std::string &s = Ink::sampleEpub();
      data = reinterpret_cast<const uint8_t *>(s.data());
      size = s.size();
      return true;
    }
  }
  return false;  // unreachable for a valid Format value
}

bool LibraryStore::loadPosition(size_t i, uint16_t &spine, uint32_t &offset) {
  spine = 0;
  offset = 0;
  if (i >= count_) return false;
  const Position &p = positions_[i];
  if (!p.saved) return false;
  spine = p.spine;
  offset = p.offset;
  return true;
}

bool LibraryStore::savePosition(size_t i, uint16_t spine, uint32_t offset,
                                 uint16_t permille) {
  if (i >= count_) return false;
  positions_[i].spine = spine;
  positions_[i].offset = offset;
  positions_[i].saved = true;
  entries_[i].permille = permille;
  return true;
}

bool LibraryStore::sdMounted() const { return false; }
const char *LibraryStore::backendName() const { return "mock"; }

bool LibraryStore::writePageIndex(size_t, size_t, uint32_t, const std::vector<uint32_t> &) {
  return false;  // no SD, nothing to write
}

#endif  // USE_INKWELL_SD
