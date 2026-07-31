#ifndef CYPHER_DESK_AVI_READER_H
#define CYPHER_DESK_AVI_READER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// Minimal MJPEG-in-AVI demuxer.
//
// Handles the two shapes this project cares about:
//   - ffmpeg output: MJPEG video plus interleaved 16-bit PCM audio, which is
//     what `scripts/convert-crowpanel-video.sh` produces.
//   - project 02's own VID_*.AVI clips, which have no audio stream at all and
//     therefore play silently rather than being rejected.
//
// Deliberately tolerant: unknown chunks are skipped, not treated as failures.
// ffmpeg writes LIST/INFO and JUNK padding, and a clip should not be
// unplayable over either. Chunk payloads are padded to an even length and that
// pad byte is not counted in the chunk size - getting that wrong lands you one
// byte into the middle of the next header, which looks exactly like a corrupt
// file.
//
// The source abstraction keeps SD_MMC out of this file so the host tests can
// drive it from a byte array.

class DeskAviSource {
 public:
  virtual ~DeskAviSource() {}
  virtual size_t read(uint8_t *destination, size_t length) = 0;
  virtual bool seek(uint32_t position) = 0;
  virtual uint32_t position() const = 0;
  virtual uint32_t size() const = 0;
};

struct DeskAviInfo {
  uint32_t microSecPerFrame = 0;
  uint32_t totalFrames = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  bool hasAudio = false;
  uint32_t audioRate = 0;
  uint16_t audioChannels = 0;
  uint16_t audioBits = 0;
  uint32_t largestChunk = 0;  // suggested read buffer size
  bool valid = false;

  uint32_t durationMs() const {
    return microSecPerFrame ? static_cast<uint32_t>((static_cast<uint64_t>(totalFrames) *
                                                     microSecPerFrame) / 1000ULL)
                            : 0;
  }
  uint32_t fpsTimes10() const {
    return microSecPerFrame ? static_cast<uint32_t>(10000000ULL / microSecPerFrame) : 0;
  }
  String describe() const;
};

class DeskAviReader {
 public:
  enum ChunkKind : uint8_t { kNone, kVideo, kAudio };

  // Parses the headers and positions at the first chunk inside 'movi'.
  bool open(DeskAviSource &source, String &reason);
  void close();
  bool isOpen() const { return open_; }
  const DeskAviInfo &info() const { return info_; }

  // Reports the next chunk's kind and payload size without consuming it.
  bool peek(ChunkKind &kind, uint32_t &size);
  // Consumes the chunk peeked above into `destination`. Returns bytes read, or
  // 0 on a short read or an oversized chunk.
  size_t read(uint8_t *destination, size_t capacity);
  // Consumes the chunk without reading its payload.
  void skip();
  // Back to the first chunk of 'movi'.
  bool rewind();

  uint32_t videoFramesRead() const { return videoFrames_; }
  uint32_t audioBytesRead() const { return audioBytes_; }

 private:
  DeskAviSource *source_ = nullptr;
  DeskAviInfo info_;
  bool open_ = false;
  uint32_t moviStart_ = 0;  // offset of the first chunk inside 'movi'
  uint32_t moviEnd_ = 0;
  uint32_t videoFrames_ = 0;
  uint32_t audioBytes_ = 0;

  // Peek state, so read()/skip() do not have to re-parse the header.
  bool peeked_ = false;
  ChunkKind peekKind_ = kNone;
  uint32_t peekSize_ = 0;
  uint32_t peekPayload_ = 0;

  bool parseHeaders(String &reason);
};

#if USE_CYPHER_DESK_SD
#include <FS.h>

class DeskAviFileSource : public DeskAviSource {
 public:
  explicit DeskAviFileSource(File &file) : file_(file) {}
  size_t read(uint8_t *destination, size_t length) override {
    return file_.read(destination, length);
  }
  bool seek(uint32_t position) override { return file_.seek(position); }
  uint32_t position() const override { return file_.position(); }
  uint32_t size() const override { return file_.size(); }

 private:
  File &file_;
};
#endif

#endif
