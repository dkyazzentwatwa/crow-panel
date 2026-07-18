#include "Nrf24Probe.h"
#include "../config/ProjectConfig.h"

namespace {
constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegEnAa = 0x01;
constexpr uint8_t kRegSetupRetr = 0x04;
constexpr uint8_t kRegRfCh = 0x05;
constexpr uint8_t kRegStatus = 0x07;
constexpr uint8_t kRegRpd = 0x09;
constexpr uint8_t kCmdFlushRx = 0xE2;
constexpr uint8_t kConfigPowerUpReceive = 0x03;
}

bool Nrf24Probe::detect(uint8_t &status) {
  status = bus_.nrfRead(kRegStatus);
  return status != 0x00 && status != 0xFF;
}

bool Nrf24Probe::startReceiveOnly() {
  // This path never writes a TX address or payload, never issues W_TX_PAYLOAD,
  // and disables all automatic acknowledgements and retries before CE is raised.
  digitalWrite(RF_LAB_NRF_CE, LOW);
  bus_.nrfWrite(kRegEnAa, 0x00);
  bus_.nrfWrite(kRegSetupRetr, 0x00);
  bus_.nrfWrite(kRegRfCh, RF_LAB_NRF_CHANNEL);
  bus_.nrfWrite(kRegStatus, 0x70);
  flushRx_();
  bus_.nrfWrite(kRegConfig, kConfigPowerUpReceive);
  delay(2);
  configured_ = true;
  return true;
}

bool Nrf24Probe::sampleActivity(uint8_t &rpd) {
  rpd = 0;
  if (!configured_) return false;
  digitalWrite(RF_LAB_NRF_CE, HIGH);
  delayMicroseconds(200);
  rpd = bus_.nrfRead(kRegRpd) & 0x01;
  digitalWrite(RF_LAB_NRF_CE, LOW);
  // Discard any FIFO state without inspecting packet bytes.
  flushRx_();
  return true;
}

void Nrf24Probe::stop() {
  digitalWrite(RF_LAB_NRF_CE, LOW);
  flushRx_();
  bus_.nrfWrite(kRegConfig, 0x00);
  configured_ = false;
}

void Nrf24Probe::flushRx_() { bus_.nrfStrobe(kCmdFlushRx); }
