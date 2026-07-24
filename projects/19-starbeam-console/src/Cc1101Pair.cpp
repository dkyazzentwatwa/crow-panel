#include "Cc1101Pair.h"

#if USE_STARBEAM_RADIOS
#include <ELECHOUSE_CC1101_SRC_DRV.h>

namespace {
const int kSs[2] = {STARBEAM_CC0_CS, STARBEAM_CC1_CS};
const int kGdo0[2] = {STARBEAM_CC0_GDO0, STARBEAM_CC1_GDO0};
byte g_sendBuffer[64] = {0};
}

void Cc1101Pair::select_(uint8_t idx) {
  idx &= 1;
  ELECHOUSE_cc1101.setSpiPin(STARBEAM_SPI_SCK, STARBEAM_SPI_MISO,
                             STARBEAM_SPI_MOSI, kSs[idx]);
  ELECHOUSE_cc1101.setGDO0(kGdo0[idx]);
}

void Cc1101Pair::applyProfile_() {
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(1);        // internal transmission mode
  ELECHOUSE_cc1101.setModulation(2);    // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(freqMhz_);
  ELECHOUSE_cc1101.setDeviation(47.60);
  ELECHOUSE_cc1101.setChannel(0);
  ELECHOUSE_cc1101.setChsp(199.95);
  ELECHOUSE_cc1101.setRxBW(812.50);
  ELECHOUSE_cc1101.setDRate(9.6);
  ELECHOUSE_cc1101.setPA(10);
  ELECHOUSE_cc1101.setSyncMode(2);
  ELECHOUSE_cc1101.setSyncWord(211, 145);
  ELECHOUSE_cc1101.setAdrChk(0);
  ELECHOUSE_cc1101.setAddr(0);
  ELECHOUSE_cc1101.setWhiteData(0);
  ELECHOUSE_cc1101.setPktFormat(0);
  ELECHOUSE_cc1101.setLengthConfig(1);
  ELECHOUSE_cc1101.setPacketLength(0);
  ELECHOUSE_cc1101.setCrc(0);
  ELECHOUSE_cc1101.setCRC_AF(0);
  ELECHOUSE_cc1101.setDcFilterOff(0);
  ELECHOUSE_cc1101.setManchester(0);
  ELECHOUSE_cc1101.setFEC(0);
  ELECHOUSE_cc1101.setPRE(0);
  ELECHOUSE_cc1101.setPQT(0);
  ELECHOUSE_cc1101.setAppendStatus(0);
}

void Cc1101Pair::begin() {
  for (uint8_t i = 0; i < 2; ++i) {
    select_(i);
    ELECHOUSE_cc1101.Init();
    present_[i] = ELECHOUSE_cc1101.getCC1101();
    if (present_[i]) applyProfile_();
  }
}

void Cc1101Pair::setFrequency(float mhz) {
  freqMhz_ = mhz;
  for (uint8_t i = 0; i < 2; ++i) {
    if (!present_[i]) continue;
    select_(i);
    ELECHOUSE_cc1101.setMHZ(mhz);
  }
}

void Cc1101Pair::jam(uint8_t idx, bool armed) {
  if (!armed) return;
  randomSeed(micros());
  for (int i = 0; i < 60; ++i) g_sendBuffer[i] = (byte)random(255);
  auto fire = [&](uint8_t i) {
    if (!present_[i]) return;
    select_(i);
    ELECHOUSE_cc1101.SendData(g_sendBuffer, 60);
  };
  if (idx >= 2) { fire(0); fire(1); }
  else fire(idx & 1);
}

float Cc1101Pair::rssi(uint8_t idx) {
  idx &= 1;
  if (!present_[idx]) return -127.0f;
  select_(idx);
  return ELECHOUSE_cc1101.getRssi();
}

int Cc1101Pair::lqi(uint8_t idx) {
  idx &= 1;
  if (!present_[idx]) return 0;
  select_(idx);
  return ELECHOUSE_cc1101.getLqi();
}

void Cc1101Pair::reset() {
  begin();
}

bool Cc1101Pair::sweepStep(float lo, float hi, float &freqOut, float &rssiOut) {
  if (sweepCursor_ < lo || sweepCursor_ > hi) sweepCursor_ = lo;
  select_(0);
  if (present_[0]) {
    ELECHOUSE_cc1101.setRxBW(58);
    ELECHOUSE_cc1101.SetRx();
    ELECHOUSE_cc1101.setMHZ(sweepCursor_);
    rssiOut = ELECHOUSE_cc1101.getRssi();
  } else {
    rssiOut = -127.0f;
  }
  freqOut = sweepCursor_;
  bool wrapped = false;
  sweepCursor_ += 0.05f;
  if (sweepCursor_ > hi) { sweepCursor_ = lo; wrapped = true; }
  return wrapped;
}

#else  // ---- stubs; register proof still runs via RadioBus ----

void Cc1101Pair::begin() {}
void Cc1101Pair::setFrequency(float mhz) { freqMhz_ = mhz; }
void Cc1101Pair::jam(uint8_t, bool) {}
float Cc1101Pair::rssi(uint8_t) { return -127.0f; }
int Cc1101Pair::lqi(uint8_t) { return 0; }
void Cc1101Pair::reset() {}
bool Cc1101Pair::sweepStep(float lo, float, float &freqOut, float &rssiOut) {
  freqOut = lo;
  rssiOut = -127.0f;
  return false;
}

#endif  // USE_STARBEAM_RADIOS
