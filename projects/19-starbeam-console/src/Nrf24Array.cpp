#include "Nrf24Array.h"

#if USE_STARBEAM_RADIOS
#include <RF24.h>

namespace {
// Five radios on the shared SPI bus; unique CE/CSN per radio.
RF24 g_radio[5] = {
    RF24(STARBEAM_NRF0_CE, STARBEAM_NRF0_CSN, STARBEAM_NRF_SPI_HZ),
    RF24(STARBEAM_NRF1_CE, STARBEAM_NRF1_CSN, STARBEAM_NRF_SPI_HZ),
    RF24(STARBEAM_NRF2_CE, STARBEAM_NRF2_CSN, STARBEAM_NRF_SPI_HZ),
    RF24(STARBEAM_NRF3_CE, STARBEAM_NRF3_CSN, STARBEAM_NRF_SPI_HZ),
    RF24(STARBEAM_NRF4_CE, STARBEAM_NRF4_CSN, STARBEAM_NRF_SPI_HZ),
};
constexpr uint8_t kSignalMax = 10;
}

void Nrf24Array::begin() {
  for (uint8_t i = 0; i < 5; ++i) {
    if (g_radio[i].begin(&bus_.spi())) {
      present_[i] = true;
      g_radio[i].setAutoAck(false);
      g_radio[i].stopListening();
      g_radio[i].setRetries(0, 0);
      g_radio[i].setPALevel(RF24_PA_MAX, true);
      g_radio[i].setDataRate(RF24_2MBPS);
      g_radio[i].setCRCLength(RF24_CRC_DISABLED);
      g_radio[i].startConstCarrier(RF24_PA_MAX, 45);
    } else {
      present_[i] = false;
    }
  }
}

void Nrf24Array::btJam(bool armed) {
  if (!armed) return;
  for (uint8_t i = 0; i < 5; ++i) if (present_[i]) g_radio[i].setChannel(random(81));
  delayMicroseconds(random(60));
}

void Nrf24Array::droneJam(bool armed) {
  if (!armed) return;
  for (uint8_t i = 0; i < 5; ++i) if (present_[i]) g_radio[i].setChannel(random(126));
  delayMicroseconds(random(60));
}

void Nrf24Array::wifiJam(bool armed) {
  if (!armed) return;
  static const int ch[] = {1, 6, 14};
  int c = ch[random(3)];
  for (uint8_t i = 0; i < 5; ++i) if (present_[i]) g_radio[i].setChannel(c);
}

void Nrf24Array::singleChannel(bool armed) {
  if (!armed) return;
  if (present_[0]) g_radio[0].setChannel(random(15));
  if (present_[1]) g_radio[1].setChannel(random(81));
  if (present_[2]) g_radio[2].setChannel(random(15));
  if (present_[3]) g_radio[3].setChannel(random(81));
  if (present_[4]) g_radio[4].setChannel(random(81));
  delayMicroseconds(random(60));
}

uint8_t Nrf24Array::sampleSpectrum(uint8_t out[128]) {
  // Use radio 0 as the receiver; sweep one channel per call.
  if (present_[0]) {
    g_radio[0].setChannel(cursor_);
    g_radio[0].startListening();
    uint8_t strength = 0;
    for (int i = 0; i < 5; ++i) {
      delayMicroseconds(130);
      if (g_radio[0].testRPD()) strength += 2;
    }
    g_radio[0].stopListening();
    if (strength > kSignalMax) strength = kSignalMax;
    history_[cursor_] = strength;
  }
  cursor_ = (cursor_ + 1) % 128;
  uint8_t peak = 0;
  for (uint8_t i = 0; i < 128; ++i) {
    out[i] = history_[i];
    if (history_[i] > peak) peak = history_[i];
  }
  return peak;
}

void Nrf24Array::stopAll() {
  for (uint8_t i = 0; i < 5; ++i) if (present_[i]) g_radio[i].stopListening();
}

#else  // ---- library-free stubs; register proof still runs via RadioBus ----

void Nrf24Array::begin() {}
void Nrf24Array::btJam(bool) {}
void Nrf24Array::droneJam(bool) {}
void Nrf24Array::wifiJam(bool) {}
void Nrf24Array::singleChannel(bool) {}
uint8_t Nrf24Array::sampleSpectrum(uint8_t out[128]) {
  for (uint8_t i = 0; i < 128; ++i) out[i] = 0;
  return 0;
}
void Nrf24Array::stopAll() {}

#endif  // USE_STARBEAM_RADIOS
