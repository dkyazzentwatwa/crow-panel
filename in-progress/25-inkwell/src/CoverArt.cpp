// COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8). NOT
// HARDWARE-VERIFIED -- the whole decode/cache path has never met a real
// card or panel. No __has_include anywhere: JPEGDEC/PNGdec are hard
// includes behind the flag gate, and the flag matrix's kitchen-sink row
// lists both libraries so a green build proves real linkage.
#include "CoverArt.h"

#if USE_DISPLAY && USE_INKWELL_SD && defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <SD_MMC.h>

#include "esp_heap_caps.h"
#include "InkTheme.h"

namespace CoverArt {

namespace {

String thumbPath(const String &bookId) {
  return String(INKWELL_CATALOG_DIR) + "/" + bookId + ".thumb";
}

// ---- decode target state. Single-threaded sketch; the decoder callbacks
// have no user pointer worth threading a context through, so file-local
// state is the honest shape here.
uint16_t *canvas_ = nullptr;   // kThumbW x kThumbH letterbox target
uint16_t *full_ = nullptr;     // JPEG path: scaled full frame before resample
int16_t fullW_ = 0, fullH_ = 0;
int16_t pngW_ = 0, pngH_ = 0;

// Letterbox mapping: destination rect inside the 220x300 canvas preserving
// the source aspect ratio.
void letterboxRect(int16_t srcW, int16_t srcH, int16_t &dx, int16_t &dy,
                   int16_t &dw, int16_t &dh) {
  // Wider than 220:300 -> pin width; else pin height.
  if ((int32_t)srcW * kThumbH >= (int32_t)srcH * kThumbW) {
    dw = kThumbW;
    dh = (int16_t)((int32_t)srcH * kThumbW / srcW);
  } else {
    dh = kThumbH;
    dw = (int16_t)((int32_t)srcW * kThumbH / srcH);
  }
  if (dw < 1) dw = 1;
  if (dh < 1) dh = 1;
  dx = (int16_t)((kThumbW - dw) / 2);
  dy = (int16_t)((kThumbH - dh) / 2);
}

int jpegDrawCb(JPEGDRAW *d) {
  // Copy the decoded MCU block into the scaled full frame.
  for (int row = 0; row < d->iHeight; ++row) {
    int16_t y = (int16_t)(d->y + row);
    if (y >= fullH_) break;
    int16_t w = d->iWidth;
    if (d->x + w > fullW_) w = (int16_t)(fullW_ - d->x);
    if (w <= 0) continue;
    memcpy(full_ + (size_t)y * fullW_ + d->x,
           d->pPixels + (size_t)row * d->iWidth, (size_t)w * 2);
  }
  return 1;  // keep decoding
}

bool decodeJpeg(const std::string &data) {
  JPEGDEC jpeg;
  if (!jpeg.openRAM((uint8_t *)data.data(), (int)data.size(), jpegDrawCb)) {
    return false;
  }
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  int w = jpeg.getWidth(), h = jpeg.getHeight();
  if (w <= 0 || h <= 0) { jpeg.close(); return false; }

  // Smallest JPEGDEC power-of-two downscale that keeps both dimensions at
  // or above the thumb box (so the NN resample below only ever shrinks).
  int scale = 1, opt = 0;
  while (scale < 8 && w / (scale * 2) >= kThumbW && h / (scale * 2) >= kThumbH) {
    scale *= 2;
  }
  if (scale == 2) opt = JPEG_SCALE_HALF;
  else if (scale == 4) opt = JPEG_SCALE_QUARTER;
  else if (scale == 8) opt = JPEG_SCALE_EIGHTH;
  fullW_ = (int16_t)(w / scale);
  fullH_ = (int16_t)(h / scale);

  full_ = (uint16_t *)heap_caps_malloc((size_t)fullW_ * fullH_ * 2,
                                       MALLOC_CAP_SPIRAM);
  if (full_ == nullptr) { jpeg.close(); return false; }
  bool ok = jpeg.decode(0, 0, opt) != 0;
  jpeg.close();
  if (!ok) { heap_caps_free(full_); full_ = nullptr; return false; }

  // Nearest-neighbour into the letterbox rect.
  int16_t dx, dy, dw, dh;
  letterboxRect(fullW_, fullH_, dx, dy, dw, dh);
  for (int16_t y = 0; y < dh; ++y) {
    int16_t sy = (int16_t)((int32_t)y * fullH_ / dh);
    for (int16_t x = 0; x < dw; ++x) {
      int16_t sx = (int16_t)((int32_t)x * fullW_ / dw);
      canvas_[(size_t)(dy + y) * kThumbW + dx + x] =
          full_[(size_t)sy * fullW_ + sx];
    }
  }
  heap_caps_free(full_);
  full_ = nullptr;
  return true;
}

// PNG path: rows arrive top-down one at a time; NN row/column selection
// straight into the canvas (no full frame held).
int16_t pngDx_, pngDy_, pngDw_, pngDh_;
uint16_t *pngLine_ = nullptr;

int pngDrawCb(PNGDRAW *d) {
  PNG *png = (PNG *)d->pUser;
  // Which destination rows does this source row feed? dst y such that
  // sy(y) == d->y  ->  y in [ceil(d->y*dh/srcH), next row's start).
  int32_t yStart = ((int32_t)d->y * pngDh_ + pngH_ - 1) / pngH_;
  int32_t yEnd = ((int32_t)(d->y + 1) * pngDh_ + pngH_ - 1) / pngH_;
  if (yEnd <= yStart) return 1;  // maps to no destination row; keep decoding
  png->getLineAsRGB565(d, pngLine_, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  for (int32_t y = yStart; y < yEnd && y < pngDh_; ++y) {
    for (int16_t x = 0; x < pngDw_; ++x) {
      int16_t sx = (int16_t)((int32_t)x * pngW_ / pngDw_);
      canvas_[(size_t)(pngDy_ + y) * kThumbW + pngDx_ + x] = pngLine_[sx];
    }
  }
  return 1;  // PNGdec 1.1.6 wants int: nonzero = keep decoding
}

bool decodePng(const std::string &data) {
  PNG png;
  if (png.openRAM((uint8_t *)data.data(), (int)data.size(), pngDrawCb) !=
      PNG_SUCCESS) {
    return false;
  }
  pngW_ = (int16_t)png.getWidth();
  pngH_ = (int16_t)png.getHeight();
  if (pngW_ <= 0 || pngH_ <= 0) { png.close(); return false; }
  letterboxRect(pngW_, pngH_, pngDx_, pngDy_, pngDw_, pngDh_);
  pngLine_ = (uint16_t *)heap_caps_malloc((size_t)pngW_ * 2, MALLOC_CAP_SPIRAM);
  if (pngLine_ == nullptr) { png.close(); return false; }
  bool ok = png.decode(&png, 0) == PNG_SUCCESS;
  png.close();
  heap_caps_free(pngLine_);
  pngLine_ = nullptr;
  return ok;
}

}  // namespace

bool drawCachedThumb(Arduino_GFX *g, const String &bookId, int16_t x,
                     int16_t y) {
  if (g == nullptr) return false;
  File f = SD_MMC.open(thumbPath(bookId), FILE_READ);
  if (!f) return false;
  uint16_t w = 0, h = 0;
  if (f.read((uint8_t *)&w, 2) != 2 || f.read((uint8_t *)&h, 2) != 2 ||
      w != kThumbW || h != kThumbH) {
    f.close();
    return false;
  }
  // One row at a time keeps the stack flat and needs no PSRAM.
  uint16_t row[kThumbW];
  for (int16_t r = 0; r < kThumbH; ++r) {
    if (f.read((uint8_t *)row, sizeof(row)) != sizeof(row)) {
      f.close();
      return false;
    }
    g->draw16bitRGBBitmap(x, (int16_t)(y + r), row, kThumbW, 1);
  }
  f.close();
  return true;
}

void generateThumb(const String &bookId, Ink::InkBook &book) {
  if (book.format() != Ink::Format::Epub) return;
  String path = thumbPath(bookId);
  if (SD_MMC.exists(path)) return;

  std::string cover, mediaType;
  if (!book.coverImage(cover, mediaType) || cover.empty()) return;

  canvas_ = (uint16_t *)heap_caps_malloc((size_t)kThumbW * kThumbH * 2,
                                         MALLOC_CAP_SPIRAM);
  if (canvas_ == nullptr) return;
  // kCard letterbox background so bars match the card the thumb sits on.
  uint16_t bg = InkTheme::to565(InkTheme::kCard);
  for (size_t i = 0; i < (size_t)kThumbW * kThumbH; ++i) canvas_[i] = bg;

  bool ok = false;
  if (mediaType.find("png") != std::string::npos) {
    ok = decodePng(cover);
  } else if (mediaType.find("jp") != std::string::npos) {  // jpeg/jpg
    ok = decodeJpeg(cover);
  }

  if (ok) {
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) {
      uint16_t w = kThumbW, h = kThumbH;
      bool wrote = f.write((uint8_t *)&w, 2) == 2 && f.write((uint8_t *)&h, 2) == 2 &&
                   f.write((uint8_t *)canvas_, (size_t)kThumbW * kThumbH * 2) ==
                       (size_t)kThumbW * kThumbH * 2;
      f.close();
      if (!wrote) SD_MMC.remove(path);  // never leave a truncated cache
    }
  }
  heap_caps_free(canvas_);
  canvas_ = nullptr;
}

}  // namespace CoverArt

#endif  // USE_DISPLAY && USE_INKWELL_SD && CONFIG_IDF_TARGET_ESP32P4
