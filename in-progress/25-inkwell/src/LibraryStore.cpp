#include "LibraryStore.h"

#include "SampleBooks.h"

namespace {
// Returned by entry() for an out-of-range index -- see the header comment.
const BookEntry kEmptyEntry;
}  // namespace

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
