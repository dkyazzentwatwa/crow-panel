#include "Cc1101Probe.h"
#include "../config/ProjectConfig.h"

namespace {
constexpr uint8_t kRegIocfg2 = 0x00;
constexpr uint8_t kRegIocfg0 = 0x02;
constexpr uint8_t kRegFreq2 = 0x0D;
constexpr uint8_t kRegFreq1 = 0x0E;
constexpr uint8_t kRegFreq0 = 0x0F;
constexpr uint8_t kStatusPartnum = 0x30;
constexpr uint8_t kStatusVersion = 0x31;
constexpr uint8_t kStatusRssi = 0x34;
constexpr uint8_t kStrobeSres = 0x30;
constexpr uint8_t kStrobeSrx = 0x34;
constexpr uint8_t kStrobeSidle = 0x36;
constexpr uint8_t kStrobeSfrx = 0x3A;
constexpr uint8_t kStrobeSpwd = 0x39;
constexpr uint8_t kGdoCarrierSense = 0x0E;
constexpr uint8_t kGdoCca = 0x09;
constexpr uint32_t kCrystalHz = 26000000UL;
}

bool Cc1101Probe::detect(uint8_t &partnum, uint8_t &version) {
  partnum = bus_.ccReadStatus(kStatusPartnum);
  version = bus_.ccReadStatus(kStatusVersion);
  return version != 0x00 && version != 0xFF;
}

bool Cc1101Probe::startReceiveOnly() {
  // All CC1101 traffic in this class is configuration, status, or RX control.
  // The TX strobe and FIFO write commands are intentionally absent.
  bus_.ccStrobe(kStrobeSres);
  delay(2);
  bus_.ccWrite(kRegIocfg0, kGdoCarrierSense);
  bus_.ccWrite(kRegIocfg2, kGdoCca);
  setFrequency_();
  flushRx_();
  bus_.ccStrobe(kStrobeSrx);
  configured_ = true;
  return true;
}

bool Cc1101Probe::sampleActivity(int16_t &rssiDbm, bool &gdo0High, bool &gdo2High) {
  rssiDbm = -127;
  gdo0High = digitalRead(RF_LAB_CC1101_GDO0) == HIGH;
  gdo2High = digitalRead(RF_LAB_CC1101_GDO2) == HIGH;
  if (!configured_) return false;
  uint8_t raw = bus_.ccReadStatus(kStatusRssi);
  int16_t signedRaw = raw >= 128 ? (int16_t)raw - 256 : raw;
  rssiDbm = signedRaw / 2 - 74;
  // Never inspect the RX FIFO. Clear it between samples so only aggregate
  // GDO/RSSI activity survives into application state.
  bus_.ccStrobe(kStrobeSidle);
  flushRx_();
  bus_.ccStrobe(kStrobeSrx);
  return true;
}

void Cc1101Probe::stop() {
  bus_.ccStrobe(kStrobeSidle);
  flushRx_();
  bus_.ccStrobe(kStrobeSpwd);
  configured_ = false;
}

void Cc1101Probe::setFrequency_() {
  uint32_t word = (uint32_t)(((uint64_t)RF_LAB_CC1101_FREQUENCY_HZ << 16) / kCrystalHz);
  bus_.ccWrite(kRegFreq2, (word >> 16) & 0xFF);
  bus_.ccWrite(kRegFreq1, (word >> 8) & 0xFF);
  bus_.ccWrite(kRegFreq0, word & 0xFF);
}

void Cc1101Probe::flushRx_() { bus_.ccStrobe(kStrobeSfrx); }
