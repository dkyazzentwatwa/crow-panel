#include "Recording.h"
#include <Preferences.h>

namespace {
Preferences g_prefs;
constexpr char kNamespace[] = "starbeam";
constexpr char kKeyBuf[] = "rec_buf";
constexpr char kKeyLen[] = "rec_len";
}

void Recording::begin() {
  load();
}

void Recording::recordRaw(int intervalUs, uint16_t count) {
  if (count > kBufferSize) count = kBufferSize;
#if USE_STARBEAM_RADIOS
  pinMode(STARBEAM_CC0_GDO0, INPUT);
  for (uint16_t i = 0; i < count; ++i) {
    uint8_t b = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      b = (b << 1) | (digitalRead(STARBEAM_CC0_GDO0) & 1);
      delayMicroseconds(intervalUs);
    }
    buf_[i] = b;
  }
  count_ = count;
#else
  (void)intervalUs;
  count_ = 0;
#endif
}

void Recording::playRaw(int intervalUs, bool armed) {
  if (!armed || count_ == 0) return;
#if USE_STARBEAM_RADIOS
  pinMode(STARBEAM_CC0_GDO0, OUTPUT);
  for (uint16_t i = 0; i < count_; ++i) {
    uint8_t b = buf_[i];
    for (int8_t bit = 7; bit >= 0; --bit) {
      digitalWrite(STARBEAM_CC0_GDO0, (b >> bit) & 1);
      delayMicroseconds(intervalUs);
    }
  }
  pinMode(STARBEAM_CC0_GDO0, INPUT);
#else
  (void)intervalUs;
#endif
}

void Recording::flush() {
  memset(buf_, 0, sizeof(buf_));
  count_ = 0;
  save();
}

bool Recording::save() {
  if (!g_prefs.begin(kNamespace, false)) return false;
  g_prefs.putUShort(kKeyLen, count_);
  g_prefs.putBytes(kKeyBuf, buf_, count_);
  g_prefs.end();
  return true;
}

bool Recording::load() {
  if (!g_prefs.begin(kNamespace, true)) return false;
  count_ = g_prefs.getUShort(kKeyLen, 0);
  if (count_ > kBufferSize) count_ = kBufferSize;
  if (count_ > 0) g_prefs.getBytes(kKeyBuf, buf_, count_);
  g_prefs.end();
  return true;
}
