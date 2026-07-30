#ifndef POKEDEX_BMP_H
#define POKEDEX_BMP_H

// Pure C++ 24bpp BMP decoder for the SD sprite pack. Free of Arduino.h so the
// shipping translation unit also builds under g++ (scripts/test-pokedex.sh).
//
// The sprite pack is uniformly Windows 3.x, 54-byte header, 24bpp, bottom-up,
// 40x40. At 40px a row is 120 bytes, already a multiple of 4, so real sprites
// carry no row padding - but stride is computed properly anyway.
#include <stdint.h>

namespace pokedex {

struct BmpInfo {
  int32_t width = 0;
  int32_t height = 0;      // Always positive; topDown records the original sign.
  uint32_t pixelOffset = 0;
  uint32_t stride = 0;
  bool topDown = false;
};

// Validates the header and fills info. Returns false for anything that is not a
// 24bpp uncompressed BMP that fits inside `length`.
bool readBmpInfo(const uint8_t *bytes, uint32_t length, BmpInfo &info);

// Decodes to RGB565, top-down, into `out` which must hold width*height pixels.
bool decodeBmp(const uint8_t *bytes, uint32_t length, uint16_t *out, uint32_t outPixels);

// Nearest-neighbour integer upscale. `scale` must be >= 1. `out` must hold
// (srcW*scale)*(srcH*scale) pixels.
bool upscaleNearest(const uint16_t *src, int32_t srcW, int32_t srcH, uint8_t scale,
                    uint16_t *out, uint32_t outPixels);

}  // namespace pokedex

#endif
