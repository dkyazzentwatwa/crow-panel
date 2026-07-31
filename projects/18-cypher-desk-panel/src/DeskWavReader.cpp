#include "DeskWavReader.h"

#include <string.h>

namespace {

uint16_t le16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t le32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool tagIs(const uint8_t *bytes, const char *tag) {
  return memcmp(bytes, tag, 4) == 0;
}

// RIFF pads every chunk payload to an even length, and the pad byte is not
// counted in the chunk size. Skipping it is the difference between walking the
// chunk list and landing one byte into the middle of the next header.
uint32_t paddedLength(uint32_t length) { return length + (length & 1u); }

constexpr uint16_t kFormatPcm = 0x0001;
constexpr uint16_t kFormatExtensible = 0xFFFE;

}  // namespace

String DeskWavFormat::describe() const {
  if (!valid) return "unreadable";
  String text;
  if (sampleRate % 1000 == 0) {
    text += String(sampleRate / 1000);
  } else {
    text += String(sampleRate / 1000);
    text += ".";
    text += String((sampleRate % 1000) / 100);
  }
  text += " kHz ";
  text += channels == 1 ? "mono" : "stereo";
  text += " ";
  text += String(bitsPerSample);
  text += "-bit";
  return text;
}

bool DeskWav::parse(DeskWavSource &source, DeskWavFormat &out, String &reason) {
  out = DeskWavFormat();

  const uint32_t fileSize = source.size();
  if (fileSize < 44) {
    reason = "file too small to be a WAV";
    return false;
  }
  if (!source.seek(0)) {
    reason = "cannot rewind file";
    return false;
  }

  uint8_t header[12] = {};
  if (source.read(header, 12) != 12 || !tagIs(header, "RIFF") || !tagIs(header + 8, "WAVE")) {
    reason = "not a RIFF/WAVE file";
    return false;
  }

  bool haveFormat = false;
  uint32_t position = 12;

  // Walk chunks until 'data' is found or the file runs out. Unknown chunk ids
  // are skipped, not rejected.
  while (position + 8 <= fileSize) {
    uint8_t chunk[8] = {};
    if (!source.seek(position) || source.read(chunk, 8) != 8) break;
    const uint32_t chunkSize = le32(chunk + 4);
    const uint32_t payload = position + 8;

    if (tagIs(chunk, "fmt ")) {
      if (chunkSize < 16) {
        reason = "truncated fmt chunk";
        return false;
      }
      uint8_t format[16] = {};
      if (source.read(format, 16) != 16) {
        reason = "truncated fmt chunk";
        return false;
      }
      uint16_t tag = le16(format);
      out.channels = le16(format + 2);
      out.sampleRate = le32(format + 4);
      out.bitsPerSample = le16(format + 14);

      // WAVE_FORMAT_EXTENSIBLE wraps the real tag in the first two bytes of a
      // 16-byte subformat GUID. ffmpeg emits this for some stereo output.
      if (tag == kFormatExtensible && chunkSize >= 40) {
        uint8_t extension[24] = {};
        if (source.read(extension, 24) == 24) tag = le16(extension + 8);
      }
      if (tag != kFormatPcm) {
        reason = "not uncompressed PCM";
        return false;
      }
      haveFormat = true;
    } else if (tagIs(chunk, "data")) {
      if (!haveFormat) {
        reason = "data chunk before fmt chunk";
        return false;
      }
      out.dataStart = payload;
      // Streamed WAVs sometimes carry a placeholder size (0 or 0xFFFFFFFF).
      // Trust the file length over the header in that case.
      uint32_t available = fileSize > payload ? fileSize - payload : 0;
      out.dataBytes = (chunkSize == 0 || chunkSize > available) ? available : chunkSize;
      if (out.dataBytes == 0) {
        reason = "WAV contains no samples";
        return false;
      }
      break;
    }

    position = payload + paddedLength(chunkSize);
  }

  if (!haveFormat) {
    reason = "no fmt chunk found";
    return false;
  }
  if (out.dataStart == 0) {
    reason = "no data chunk found";
    return false;
  }
  if (out.channels != 1 && out.channels != 2) {
    reason = String("need mono or stereo, got ") + out.channels + " channels";
    return false;
  }
  if (out.bitsPerSample != 8 && out.bitsPerSample != 16) {
    reason = String("need 8- or 16-bit, got ") + out.bitsPerSample;
    return false;
  }
  if (out.sampleRate < DeskWav::kMinRate || out.sampleRate > DeskWav::kMaxRate) {
    reason = String("sample rate ") + out.sampleRate + " outside " + DeskWav::kMinRate + "-" +
             DeskWav::kMaxRate;
    return false;
  }

  // Never hand the mixer a partial frame.
  out.dataBytes -= out.dataBytes % out.frameBytes();
  if (out.dataBytes == 0) {
    reason = "WAV contains no complete frames";
    return false;
  }

  out.valid = true;
  reason = out.describe();
  source.seek(out.dataStart);
  return true;
}
