#ifndef CYPHER_DESK_WAV_READER_H
#define CYPHER_DESK_WAV_READER_H

#include "../config/ProjectConfig.h"
#include <Arduino.h>

// One RIFF/WAVE parser for the whole project.
//
// This replaces two near-identical copies that each hardcoded
// "PCM, mono, 16-bit, exactly CYPHER_DESK_AUDIO_SAMPLE_RATE" and rejected
// everything else. The mixer resamples now, so the parser's job is to report
// what a file actually is, not to enforce one shape.
//
// Deliberately tolerant about chunk layout: anything that is not 'fmt ' or
// 'data' is skipped rather than treated as a parse failure. ffmpeg writes a
// LIST/INFO chunk between them unless you pass `-map_metadata -1`, and a card
// full of otherwise-valid music should not be unplayable over that.
//
// The source abstraction keeps this file free of SD_MMC and FS.h so the same
// code runs under the host test harness (scripts/test-cypher-desk.sh).

struct DeskWavFormat {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataStart = 0;  // byte offset of the first sample
  uint32_t dataBytes = 0;  // length of the sample data, clamped to the file
  bool valid = false;

  uint32_t frameBytes() const {
    return static_cast<uint32_t>(channels) * (bitsPerSample / 8);
  }
  uint32_t frames() const {
    uint32_t bytes = frameBytes();
    return bytes ? dataBytes / bytes : 0;
  }
  uint32_t durationMs() const {
    return sampleRate ? static_cast<uint32_t>((static_cast<uint64_t>(frames()) * 1000ULL) /
                                              sampleRate)
                      : 0;
  }
  // "44.1 kHz stereo 16-bit" - shown in the Music app instead of a hardcoded
  // claim about what the card is supposed to contain.
  String describe() const;
};

// Random-access byte source. The SD adapter is DeskWavFileSource below; the
// host tests supply an in-memory one.
class DeskWavSource {
 public:
  virtual ~DeskWavSource() {}
  virtual size_t read(uint8_t *destination, size_t length) = 0;
  virtual bool seek(uint32_t position) = 0;
  virtual uint32_t size() const = 0;
};

namespace DeskWav {

// What the mixer can consume. Outside this range parse() fails with a reason
// rather than playing noise.
constexpr uint32_t kMinRate = 8000;
constexpr uint32_t kMaxRate = 48000;

// Seeks `source` to 0 and walks the chunk list. On success `out.valid` is true
// and the source is left positioned at out.dataStart. On failure `reason` holds
// a short explanation fit for a status line.
bool parse(DeskWavSource &source, DeskWavFormat &out, String &reason);

}  // namespace DeskWav

#if USE_CYPHER_DESK_SD
#include <FS.h>

// Thin adapter so an open SD file can be handed straight to DeskWav::parse.
class DeskWavFileSource : public DeskWavSource {
 public:
  explicit DeskWavFileSource(File &file) : file_(file) {}
  size_t read(uint8_t *destination, size_t length) override {
    return file_.read(destination, length);
  }
  bool seek(uint32_t position) override { return file_.seek(position); }
  uint32_t size() const override { return file_.size(); }

 private:
  File &file_;
};
#endif

#endif
