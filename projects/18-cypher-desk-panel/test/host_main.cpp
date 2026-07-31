// Host tests for the three parsers Cypher Desk gained: the WAV reader, the
// MJPEG/AVI demuxer, and the editor's word-wrap engine.
//
// These are the highest-risk code in the project and the hardest to see
// failing on glass - a mis-parsed chunk header looks exactly like a corrupt
// file, and an off-by-one in the wrap table puts the caret in the wrong place
// with no error anywhere. All three are free of SD_MMC and display headers
// precisely so the shipping translation units can be driven from here.
//
// Build and run with scripts/test-cypher-desk.sh.

#include "../src/DeskAviReader.h"
#include "../src/DeskTextWrap.h"
#include "../src/DeskWavReader.h"

#include <stdio.h>

#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const char *what) {
  ++gChecks;
  if (!condition) {
    ++gFailures;
    printf("  FAIL  %s\n", what);
  }
}

void checkEqual(long actual, long expected, const char *what) {
  ++gChecks;
  if (actual != expected) {
    ++gFailures;
    printf("  FAIL  %s (got %ld, expected %ld)\n", what, actual, expected);
  }
}

void checkText(const String &actual, const char *expected, const char *what) {
  ++gChecks;
  if (!(actual == expected)) {
    ++gFailures;
    printf("  FAIL  %s (got \"%s\", expected \"%s\")\n", what, actual.c_str(), expected);
  }
}

// --- byte sources ----------------------------------------------------------

class MemorySource : public DeskWavSource, public DeskAviSource {
 public:
  explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  size_t read(uint8_t *destination, size_t length) override {
    const size_t available = bytes_.size() - position_;
    const size_t take = length < available ? length : available;
    memcpy(destination, bytes_.data() + position_, take);
    position_ += take;
    return take;
  }
  bool seek(uint32_t position) override {
    if (position > bytes_.size()) return false;
    position_ = position;
    return true;
  }
  uint32_t position() const override { return static_cast<uint32_t>(position_); }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }

 private:
  std::vector<uint8_t> bytes_;
  size_t position_ = 0;
};

// --- fixture builders ------------------------------------------------------

void putTag(std::vector<uint8_t> &out, const char *tag) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(tag[i]));
}
void put16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(v & 0xff);
  out.push_back((v >> 8) & 0xff);
}
void put32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(v & 0xff);
  out.push_back((v >> 8) & 0xff);
  out.push_back((v >> 16) & 0xff);
  out.push_back((v >> 24) & 0xff);
}
void patch32(std::vector<uint8_t> &out, size_t at, uint32_t v) {
  out[at] = v & 0xff;
  out[at + 1] = (v >> 8) & 0xff;
  out[at + 2] = (v >> 16) & 0xff;
  out[at + 3] = (v >> 24) & 0xff;
}

struct WavOptions {
  uint16_t format = 1;
  uint16_t channels = 1;
  uint32_t rate = 16000;
  uint16_t bits = 16;
  uint32_t dataBytes = 32;
  bool listChunk = false;     // ffmpeg's LIST/INFO between fmt and data
  bool oddListChunk = false;  // odd payload, so the pad byte matters
  bool zeroDataSize = false;  // streamed WAVs write a placeholder
};

std::vector<uint8_t> buildWav(const WavOptions &options) {
  std::vector<uint8_t> out;
  putTag(out, "RIFF");
  const size_t riffSizeAt = out.size();
  put32(out, 0);
  putTag(out, "WAVE");

  putTag(out, "fmt ");
  put32(out, 16);
  put16(out, options.format);
  put16(out, options.channels);
  put32(out, options.rate);
  put32(out, options.rate * options.channels * (options.bits / 8));
  put16(out, static_cast<uint16_t>(options.channels * (options.bits / 8)));
  put16(out, options.bits);

  if (options.listChunk) {
    const uint32_t payload = options.oddListChunk ? 11 : 12;
    putTag(out, "LIST");
    put32(out, payload);
    putTag(out, "INFO");
    for (uint32_t i = 4; i < payload; ++i) out.push_back('x');
    if (payload & 1u) out.push_back(0);  // RIFF pad byte
  }

  putTag(out, "data");
  put32(out, options.zeroDataSize ? 0 : options.dataBytes);
  for (uint32_t i = 0; i < options.dataBytes; ++i) out.push_back(static_cast<uint8_t>(i));

  patch32(out, riffSizeAt, static_cast<uint32_t>(out.size() - 8));
  return out;
}

struct AviOptions {
  uint32_t microSecPerFrame = 66666;  // 15 fps
  uint32_t totalFrames = 3;
  uint16_t width = 512;
  uint16_t height = 288;
  bool audio = true;
  uint32_t audioRate = 44100;
  uint16_t audioChannels = 2;
  uint16_t audioBits = 16;
  bool junkChunk = true;
  bool oddVideoChunk = false;  // forces the chunk-padding path
  bool recList = false;        // some writers wrap frame groups in LIST 'rec '
};

std::vector<uint8_t> buildAvi(const AviOptions &options) {
  std::vector<uint8_t> out;
  putTag(out, "RIFF");
  const size_t riffSizeAt = out.size();
  put32(out, 0);
  putTag(out, "AVI ");

  putTag(out, "LIST");
  const size_t hdrlSizeAt = out.size();
  put32(out, 0);
  const size_t hdrlStart = out.size();
  putTag(out, "hdrl");

  putTag(out, "avih");
  put32(out, 56);
  put32(out, options.microSecPerFrame);
  put32(out, 0);
  put32(out, 0);
  put32(out, 0x10);
  put32(out, options.totalFrames);
  put32(out, 0);
  put32(out, options.audio ? 2 : 1);
  put32(out, 65536);
  put32(out, options.width);
  put32(out, options.height);
  for (int i = 0; i < 4; ++i) put32(out, 0);

  // Video stream header.
  putTag(out, "LIST");
  const size_t strlSizeAt = out.size();
  put32(out, 0);
  const size_t strlStart = out.size();
  putTag(out, "strl");
  putTag(out, "strh");
  put32(out, 56);
  putTag(out, "vids");
  putTag(out, "MJPG");
  for (int i = 0; i < 12; ++i) put32(out, 0);
  putTag(out, "strf");
  put32(out, 40);
  for (int i = 0; i < 10; ++i) put32(out, 0);
  patch32(out, strlSizeAt, static_cast<uint32_t>(out.size() - strlStart));

  if (options.audio) {
    putTag(out, "LIST");
    const size_t audioStrlSizeAt = out.size();
    put32(out, 0);
    const size_t audioStrlStart = out.size();
    putTag(out, "strl");
    putTag(out, "strh");
    put32(out, 56);
    putTag(out, "auds");
    put32(out, 1);
    for (int i = 0; i < 12; ++i) put32(out, 0);
    putTag(out, "strf");
    put32(out, 16);
    put16(out, 1);  // WAVE_FORMAT_PCM
    put16(out, options.audioChannels);
    put32(out, options.audioRate);
    put32(out, options.audioRate * options.audioChannels * (options.audioBits / 8));
    put16(out, static_cast<uint16_t>(options.audioChannels * (options.audioBits / 8)));
    put16(out, options.audioBits);
    patch32(out, audioStrlSizeAt, static_cast<uint32_t>(out.size() - audioStrlStart));
  }
  patch32(out, hdrlSizeAt, static_cast<uint32_t>(out.size() - hdrlStart));

  if (options.junkChunk) {
    putTag(out, "JUNK");
    put32(out, 8);
    for (int i = 0; i < 8; ++i) out.push_back(0);
  }

  putTag(out, "LIST");
  const size_t moviSizeAt = out.size();
  put32(out, 0);
  const size_t moviStart = out.size();
  putTag(out, "movi");

  if (options.recList) {
    putTag(out, "LIST");
    put32(out, 4);
    putTag(out, "rec ");
  }

  for (uint32_t frame = 0; frame < options.totalFrames; ++frame) {
    const uint32_t videoBytes = options.oddVideoChunk ? 9 : 10;
    putTag(out, "00dc");
    put32(out, videoBytes);
    for (uint32_t i = 0; i < videoBytes; ++i) out.push_back(static_cast<uint8_t>(0xC0 + i));
    if (videoBytes & 1u) out.push_back(0);
    if (options.audio) {
      putTag(out, "01wb");
      put32(out, 8);
      for (uint32_t i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(i));
    }
  }
  patch32(out, moviSizeAt, static_cast<uint32_t>(out.size() - moviStart));
  patch32(out, riffSizeAt, static_cast<uint32_t>(out.size() - 8));
  return out;
}

// --- suites ----------------------------------------------------------------

void testWavReader() {
  printf("WAV reader\n");
  {
    MemorySource source(buildWav({}));
    DeskWavFormat format;
    String reason;
    check(DeskWav::parse(source, format, reason), "16 kHz mono 16-bit parses");
    checkEqual(format.sampleRate, 16000, "sample rate");
    checkEqual(format.channels, 1, "channels");
    checkEqual(format.bitsPerSample, 16, "bit depth");
    checkEqual(format.dataBytes, 32, "data length");
    checkEqual(format.frames(), 16, "frame count");
    checkText(format.describe(), "16 kHz mono 16-bit", "describe()");
  }
  {
    WavOptions options;
    options.channels = 2;
    options.rate = 44100;
    options.dataBytes = 400;
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(DeskWav::parse(source, format, reason), "44.1 kHz stereo parses");
    checkText(format.describe(), "44.1 kHz stereo 16-bit", "describe() for 44.1 stereo");
    checkEqual(format.frames(), 100, "stereo frame count");
  }
  {
    // The regression that motivated the rewrite: ffmpeg writes LIST/INFO
    // between fmt and data unless you pass -map_metadata -1, and the old
    // parser treated that as a broken file.
    WavOptions options;
    options.listChunk = true;
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(DeskWav::parse(source, format, reason), "LIST/INFO between fmt and data is skipped");
  }
  {
    // Odd-length chunk: the pad byte is not counted in the size field, so a
    // parser that ignores it lands one byte inside the next header.
    WavOptions options;
    options.listChunk = true;
    options.oddListChunk = true;
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(DeskWav::parse(source, format, reason), "odd-length chunk padding is honoured");
    checkEqual(format.dataBytes, 32, "data length after an odd chunk");
  }
  {
    WavOptions options;
    options.zeroDataSize = true;
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(DeskWav::parse(source, format, reason), "placeholder data size falls back to file length");
    checkEqual(format.dataBytes, 32, "recovered data length");
  }
  {
    WavOptions options;
    options.bits = 24;
    options.dataBytes = 48;
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(!DeskWav::parse(source, format, reason), "24-bit is refused");
    check(reason.indexOf("24") >= 0, "24-bit refusal names the bit depth");
  }
  {
    WavOptions options;
    options.rate = 96000;
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(!DeskWav::parse(source, format, reason), "96 kHz is refused");
  }
  {
    WavOptions options;
    options.format = 3;  // IEEE float
    MemorySource source(buildWav(options));
    DeskWavFormat format;
    String reason;
    check(!DeskWav::parse(source, format, reason), "non-PCM is refused");
  }
  {
    MemorySource source(std::vector<uint8_t>(200, 0));
    DeskWavFormat format;
    String reason;
    check(!DeskWav::parse(source, format, reason), "a non-RIFF file is refused");
  }
}

void testAviReader() {
  printf("AVI reader\n");
  {
    MemorySource source(buildAvi({}));
    DeskAviReader reader;
    String reason;
    check(reader.open(source, reason), "MJPEG + PCM AVI parses");
    checkEqual(reader.info().width, 512, "width");
    checkEqual(reader.info().height, 288, "height");
    checkEqual(reader.info().microSecPerFrame, 66666, "frame interval");
    checkEqual(reader.info().totalFrames, 3, "frame count");
    check(reader.info().hasAudio, "audio stream detected");
    checkEqual(reader.info().audioRate, 44100, "audio rate");
    checkEqual(reader.info().audioChannels, 2, "audio channels");
    checkEqual(reader.info().fpsTimes10(), 150, "fps x10");

    uint8_t buffer[64];
    DeskAviReader::ChunkKind kind = DeskAviReader::kNone;
    uint32_t size = 0;
    check(reader.peek(kind, size), "first chunk peeks");
    checkEqual(kind, DeskAviReader::kVideo, "first chunk is video");
    checkEqual(size, 10, "first video chunk size");
    checkEqual(static_cast<long>(reader.read(buffer, sizeof(buffer))), 10, "video chunk reads");
    checkEqual(buffer[0], 0xC0, "video payload starts where the header ends");

    check(reader.peek(kind, size), "second chunk peeks");
    checkEqual(kind, DeskAviReader::kAudio, "second chunk is audio");
    checkEqual(static_cast<long>(reader.read(buffer, sizeof(buffer))), 8, "audio chunk reads");

    uint32_t video = 1;
    while (reader.peek(kind, size)) {
      if (kind == DeskAviReader::kVideo) ++video;
      reader.skip();
    }
    checkEqual(video, 3, "all video chunks are walked");
  }
  {
    // Odd payloads exercise the same pad-byte rule as the WAV reader; get it
    // wrong and every chunk after the first is garbage.
    AviOptions options;
    options.oddVideoChunk = true;
    MemorySource source(buildAvi(options));
    DeskAviReader reader;
    String reason;
    check(reader.open(source, reason), "odd-length frame chunks parse");
    uint8_t buffer[64];
    DeskAviReader::ChunkKind kind = DeskAviReader::kNone;
    uint32_t size = 0;
    uint32_t video = 0, audio = 0;
    while (reader.peek(kind, size)) {
      if (kind == DeskAviReader::kVideo) {
        checkEqual(static_cast<long>(reader.read(buffer, sizeof(buffer))), 9, "odd video chunk");
        ++video;
      } else {
        reader.skip();
        ++audio;
      }
    }
    checkEqual(video, 3, "odd chunks do not desynchronise the walk");
    checkEqual(audio, 3, "audio chunks still found after odd video chunks");
  }
  {
    AviOptions options;
    options.recList = true;
    MemorySource source(buildAvi(options));
    DeskAviReader reader;
    String reason;
    check(reader.open(source, reason), "AVI with a rec list parses");
    DeskAviReader::ChunkKind kind = DeskAviReader::kNone;
    uint32_t size = 0;
    check(reader.peek(kind, size), "reader descends into LIST 'rec '");
    checkEqual(kind, DeskAviReader::kVideo, "first chunk inside rec is video");
  }
  {
    // Project 02 records clips with no audio stream. They must play silently,
    // not be refused.
    AviOptions options;
    options.audio = false;
    MemorySource source(buildAvi(options));
    DeskAviReader reader;
    String reason;
    check(reader.open(source, reason), "video-only AVI parses");
    check(!reader.info().hasAudio, "video-only AVI reports no audio");
    check(reason.indexOf("silent") >= 0, "describe() says silent");
  }
  {
    MemorySource source(std::vector<uint8_t>(200, 0));
    DeskAviReader reader;
    String reason;
    check(!reader.open(source, reason), "a non-AVI file is refused");
  }
}

void testTextWrap() {
  printf("Text wrap\n");
  {
    DeskTextWrap wrap;
    wrap.rebuild(String("one\ntwo\nthree"), 78);
    checkEqual(wrap.lineCount(), 3, "hard newlines split lines");
    checkText(wrap.lineText(String("one\ntwo\nthree"), 0), "one", "first line");
    checkText(wrap.lineText(String("one\ntwo\nthree"), 1), "two", "second line");
    checkText(wrap.lineText(String("one\ntwo\nthree"), 2), "three", "third line");
  }
  {
    // Word lengths must NOT divide evenly into the column count, or a forced
    // cut lands in the same place a space-break would and the test proves
    // nothing. (The first version of this fixture was 5-character words at 20
    // columns, and it passed with break-on-space disabled.)
    String text;
    for (int i = 0; i < 8; ++i) text += "alpha bravo charlie delta echo foxtrot ";
    DeskTextWrap wrap;
    wrap.rebuild(text, 20);
    check(wrap.lineCount() > 1, "a long paragraph wraps");
    bool withinColumns = true;
    for (uint16_t line = 0; line < wrap.lineCount(); ++line) {
      if (wrap.lineText(text, line).length() > 20) withinColumns = false;
    }
    check(withinColumns, "no display line exceeds the column count");

    // Length alone does not prove words stayed whole - a parser that ignored
    // spaces entirely would also pass that. Every soft break in this fixture
    // must land immediately after a space.
    bool brokeOnSpaces = true;
    for (uint16_t line = 1; line < wrap.lineCount(); ++line) {
      const size_t start = wrap.lineStart(line);
      if (start == 0 || text[start - 1] != ' ') brokeOnSpaces = false;
    }
    check(brokeOnSpaces, "soft breaks land after a space, keeping words whole");
  }
  {
    // A single word longer than a row has to be cut at the row edge; the
    // alternative is a line that runs off the panel.
    String text("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    DeskTextWrap wrap;
    wrap.rebuild(text, 10);
    checkEqual(wrap.lineCount(), 3, "an over-long word is cut at the row edge");
  }
  {
    String text("hello world this is a wrapped paragraph of text");
    DeskTextWrap wrap;
    wrap.rebuild(text, 12);
    // The caret has to survive a round trip, or tap-to-place lands elsewhere.
    bool roundTrips = true;
    for (size_t index = 0; index <= text.length(); ++index) {
      const uint16_t line = wrap.lineForIndex(index);
      const uint16_t column = wrap.columnForIndex(index);
      if (wrap.indexFor(line, column) != index) roundTrips = false;
    }
    check(roundTrips, "index -> line/column -> index round-trips");
  }
  {
    DeskTextWrap wrap;
    wrap.rebuild(String(""), 78);
    checkEqual(wrap.lineCount(), 1, "an empty document still has one line");
    checkEqual(wrap.lineForIndex(0), 0, "cursor at 0 in an empty document");
  }
}

// --- real-file mode --------------------------------------------------------
// Synthetic fixtures prove the parser handles the shapes I thought of. Running
// a real ffmpeg or project-02 file through the SAME translation unit that
// ships is what proves it handles the shapes they actually emit - and it lets
// anyone check a clip before copying it to the card.

std::vector<uint8_t> readFile(const char *path, bool &ok) {
  std::vector<uint8_t> bytes;
  FILE *file = fopen(path, "rb");
  ok = file != nullptr;
  if (!ok) return bytes;
  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
  if (!bytes.empty() && fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) ok = false;
  fclose(file);
  return bytes;
}

int inspect(const char *path) {
  bool ok = false;
  std::vector<uint8_t> bytes = readFile(path, ok);
  if (!ok) {
    printf("cannot read %s\n", path);
    return 1;
  }
  printf("%s (%zu bytes)\n", path, bytes.size());

  const String name(path);
  String lower(name);
  lower.toLowerCase();

  if (lower.endsWith(".wav")) {
    MemorySource source(std::move(bytes));
    DeskWavFormat format;
    String reason;
    if (!DeskWav::parse(source, format, reason)) {
      printf("  REJECTED: %s\n", reason.c_str());
      return 1;
    }
    printf("  %s, %u frames, %u ms\n", format.describe().c_str(), format.frames(),
           format.durationMs());
    return 0;
  }

  MemorySource source(std::move(bytes));
  DeskAviReader reader;
  String reason;
  if (!reader.open(source, reason)) {
    printf("  REJECTED: %s\n", reason.c_str());
    return 1;
  }
  printf("  %s, %u ms declared\n", reader.info().describe().c_str(), reader.info().durationMs());

  uint32_t video = 0, audio = 0, videoBytes = 0, audioBytes = 0, largest = 0;
  DeskAviReader::ChunkKind kind = DeskAviReader::kNone;
  uint32_t size = 0;
  while (reader.peek(kind, size)) {
    if (kind == DeskAviReader::kVideo) {
      ++video;
      videoBytes += size;
      if (size > largest) largest = size;
    } else {
      ++audio;
      audioBytes += size;
    }
    reader.skip();
  }
  printf("  walked %u video chunks (%u KB, largest %u B) and %u audio chunks (%u KB)\n", video,
         videoBytes / 1024, largest, audio, audioBytes / 1024);
  if (video != reader.info().totalFrames) {
    printf("  NOTE: walked %u frames but the header declares %u\n", video,
           reader.info().totalFrames);
  }
  return video > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc > 1) return inspect(argv[1]);

  printf("Cypher Desk host tests\n\n");
  testWavReader();
  testAviReader();
  testTextWrap();
  printf("\n%d checks, %d failure%s\n", gChecks, gFailures, gFailures == 1 ? "" : "s");
  if (gFailures == 0) {
    printf("\nPass a .avi or .wav path to inspect a real file with these parsers.\n");
  }
  return gFailures == 0 ? 0 : 1;
}
