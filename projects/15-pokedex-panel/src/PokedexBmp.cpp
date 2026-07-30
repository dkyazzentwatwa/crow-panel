#include "PokedexBmp.h"

namespace pokedex {
namespace {

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint16_t toRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace

bool readBmpInfo(const uint8_t *bytes, uint32_t length, BmpInfo &info) {
  if (bytes == nullptr || length < 54) return false;
  if (bytes[0] != 'B' || bytes[1] != 'M') return false;

  const uint16_t bpp = le16(bytes + 28);
  if (bpp != 24) return false;
  if (le32(bytes + 30) != 0) return false;  // BI_RGB only.

  const int32_t width = (int32_t)le32(bytes + 18);
  const int32_t rawHeight = (int32_t)le32(bytes + 22);
  if (width <= 0 || rawHeight == 0) return false;

  info.width = width;
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;
  info.pixelOffset = le32(bytes + 10);
  info.stride = (uint32_t)((width * 3 + 3) & ~3);

  if (info.pixelOffset < 54 || info.pixelOffset > length) return false;
  const uint32_t needed = info.stride * (uint32_t)info.height;
  if (needed > length - info.pixelOffset) return false;
  return true;
}

bool decodeBmp(const uint8_t *bytes, uint32_t length, uint16_t *out, uint32_t outPixels) {
  BmpInfo info;
  if (!readBmpInfo(bytes, length, info)) return false;
  if (out == nullptr) return false;
  const uint32_t pixels = (uint32_t)info.width * (uint32_t)info.height;
  if (outPixels < pixels) return false;

  for (int32_t y = 0; y < info.height; y++) {
    // BMP rows are bottom-up unless the height was negative.
    const int32_t fileRow = info.topDown ? y : (info.height - 1 - y);
    const uint8_t *row = bytes + info.pixelOffset + info.stride * (uint32_t)fileRow;
    uint16_t *dest = out + (uint32_t)y * (uint32_t)info.width;
    for (int32_t x = 0; x < info.width; x++) {
      // 24bpp BMP stores BGR.
      dest[x] = toRgb565(row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]);
    }
  }
  return true;
}

bool upscaleNearest(const uint16_t *src, int32_t srcW, int32_t srcH, uint8_t scale,
                    uint16_t *out, uint32_t outPixels) {
  if (src == nullptr || out == nullptr || scale == 0) return false;
  if (srcW <= 0 || srcH <= 0) return false;
  const uint32_t dstW = (uint32_t)srcW * scale;
  const uint32_t dstH = (uint32_t)srcH * scale;
  if (outPixels < dstW * dstH) return false;

  for (uint32_t y = 0; y < dstH; y++) {
    const uint16_t *srcRow = src + (uint32_t)srcW * (y / scale);
    uint16_t *dstRow = out + dstW * y;
    for (uint32_t x = 0; x < dstW; x++) {
      dstRow[x] = srcRow[x / scale];
    }
  }
  return true;
}

}  // namespace pokedex
