#include "DeskAviReader.h"

#include <string.h>

namespace {

uint16_t le16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t le32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool tagIs(const uint8_t *bytes, const char *tag) { return memcmp(bytes, tag, 4) == 0; }

// RIFF pads every chunk payload to an even length; the pad byte is not counted
// in the size field.
uint32_t padded(uint32_t length) { return length + (length & 1u); }

}  // namespace

String DeskAviInfo::describe() const {
  if (!valid) return "unreadable";
  String text = String(width) + "x" + height;
  const uint32_t fps = fpsTimes10();
  text += " ";
  text += String(fps / 10);
  if (fps % 10) {
    text += ".";
    text += String(fps % 10);
  }
  text += " fps";
  if (hasAudio) {
    text += ", ";
    text += String(audioRate / 1000);
    if (audioRate % 1000 >= 100) {
      text += ".";
      text += String((audioRate % 1000) / 100);
    }
    text += " kHz ";
    text += audioChannels == 1 ? "mono" : "stereo";
  } else {
    text += ", silent";
  }
  return text;
}

bool DeskAviReader::open(DeskAviSource &source, String &reason) {
  close();
  source_ = &source;
  if (!parseHeaders(reason)) {
    source_ = nullptr;
    return false;
  }
  open_ = true;
  reason = info_.describe();
  return true;
}

void DeskAviReader::close() {
  source_ = nullptr;
  open_ = false;
  peeked_ = false;
  videoFrames_ = 0;
  audioBytes_ = 0;
  info_ = DeskAviInfo();
}

bool DeskAviReader::parseHeaders(String &reason) {
  const uint32_t fileSize = source_->size();
  if (fileSize < 64) {
    reason = "file too small to be an AVI";
    return false;
  }
  uint8_t header[12] = {};
  if (!source_->seek(0) || source_->read(header, 12) != 12 || !tagIs(header, "RIFF") ||
      !tagIs(header + 8, "AVI ")) {
    reason = "not a RIFF/AVI file";
    return false;
  }

  bool haveMainHeader = false;
  // Which stream index carries video, so 'NNdc'/'NNwb' are matched by the two
  // TYPE characters rather than by assuming video is stream 0.
  uint32_t position = 12;

  while (position + 8 <= fileSize) {
    uint8_t chunk[8] = {};
    if (!source_->seek(position) || source_->read(chunk, 8) != 8) break;
    const uint32_t chunkSize = le32(chunk + 4);
    const uint32_t payload = position + 8;

    if (tagIs(chunk, "LIST")) {
      uint8_t listType[4] = {};
      if (source_->read(listType, 4) != 4) break;
      if (tagIs(listType, "movi")) {
        moviStart_ = payload + 4;
        moviEnd_ = payload + chunkSize;
        if (moviEnd_ > fileSize) moviEnd_ = fileSize;
        break;  // headers done; everything after this is frame data
      }
      // hdrl and strl are containers: descend into them rather than skipping.
      position = payload + 4;
      continue;
    }

    if (tagIs(chunk, "avih") && chunkSize >= 40) {
      uint8_t main[40] = {};
      if (source_->read(main, 40) != 40) break;
      info_.microSecPerFrame = le32(main);
      info_.totalFrames = le32(main + 16);
      info_.largestChunk = le32(main + 28);
      info_.width = static_cast<uint16_t>(le32(main + 32));
      info_.height = static_cast<uint16_t>(le32(main + 36));
      haveMainHeader = true;
    } else if (tagIs(chunk, "strh") && chunkSize >= 8) {
      uint8_t streamHeader[8] = {};
      if (source_->read(streamHeader, 8) == 8 && tagIs(streamHeader, "auds")) {
        info_.hasAudio = true;
      }
    } else if (tagIs(chunk, "strf") && info_.hasAudio && info_.audioRate == 0 && chunkSize >= 16) {
      // The strf right after an 'auds' strh is a WAVEFORMATEX.
      uint8_t format[16] = {};
      if (source_->read(format, 16) == 16) {
        info_.audioChannels = le16(format + 2);
        info_.audioRate = le32(format + 4);
        info_.audioBits = le16(format + 14);
        if (le16(format) != 1) {
          reason = "AVI audio track is not uncompressed PCM";
          return false;
        }
      }
    }

    position = payload + padded(chunkSize);
  }

  if (!haveMainHeader) {
    reason = "no AVI main header";
    return false;
  }
  if (moviStart_ == 0) {
    reason = "no movi list";
    return false;
  }
  if (info_.microSecPerFrame == 0) {
    reason = "AVI declares no frame rate";
    return false;
  }
  // An audio stream declared but never described is worse than none: it would
  // read as silence and desync the clock. Treat the clip as silent instead.
  if (info_.hasAudio && (info_.audioRate == 0 || info_.audioChannels == 0)) {
    info_.hasAudio = false;
  }
  info_.valid = true;
  if (!source_->seek(moviStart_)) {
    reason = "cannot seek to the frame data";
    return false;
  }
  return true;
}

bool DeskAviReader::peek(ChunkKind &kind, uint32_t &size) {
  if (!open_) return false;
  if (peeked_) {
    kind = peekKind_;
    size = peekSize_;
    return true;
  }
  // Skip anything that is not a stream chunk - ffmpeg pads with JUNK, and some
  // writers put a nested LIST 'rec ' around each frame group.
  while (true) {
    const uint32_t position = source_->position();
    if (position + 8 > moviEnd_) return false;
    uint8_t chunk[8] = {};
    if (source_->read(chunk, 8) != 8) return false;
    const uint32_t chunkSize = le32(chunk + 4);
    const uint32_t payload = position + 8;

    if (tagIs(chunk, "LIST")) {
      // Descend: 'rec ' groups hold the real chunks.
      if (!source_->seek(payload + 4)) return false;
      continue;
    }
    const bool isVideo = memcmp(chunk + 2, "dc", 2) == 0 || memcmp(chunk + 2, "db", 2) == 0;
    const bool isAudio = memcmp(chunk + 2, "wb", 2) == 0;
    if (isVideo || isAudio) {
      peeked_ = true;
      peekKind_ = isVideo ? kVideo : kAudio;
      peekSize_ = chunkSize;
      peekPayload_ = payload;
      kind = peekKind_;
      size = peekSize_;
      return true;
    }
    if (!source_->seek(payload + padded(chunkSize))) return false;
  }
}

size_t DeskAviReader::read(uint8_t *destination, size_t capacity) {
  ChunkKind kind = kNone;
  uint32_t size = 0;
  if (!peek(kind, size)) return 0;
  if (size > capacity) {
    // Too big for the caller's buffer. Skip it rather than half-reading a frame
    // and handing back something that is not a JPEG.
    skip();
    return 0;
  }
  if (!source_->seek(peekPayload_)) return 0;
  const size_t got = source_->read(destination, size);
  source_->seek(peekPayload_ + padded(size));
  peeked_ = false;
  if (got != size) return 0;
  if (kind == kVideo) ++videoFrames_;
  else audioBytes_ += got;
  return got;
}

void DeskAviReader::skip() {
  ChunkKind kind = kNone;
  uint32_t size = 0;
  if (!peek(kind, size)) return;
  source_->seek(peekPayload_ + padded(size));
  peeked_ = false;
}

bool DeskAviReader::rewind() {
  if (!open_) return false;
  peeked_ = false;
  videoFrames_ = 0;
  audioBytes_ = 0;
  return source_->seek(moviStart_);
}
