// SX1262 path: COMPILE-VERIFIED on esp32:esp32:esp32p4 (core 3.3.8) with
// RadioLib 7.x. NOT HARDWARE-VERIFIED. Radio parameters mirror Elecrow's
// own Lesson13_TX_SX1262_Wireless_Module example (RadioLib is Elecrow's
// library choice too). Pins come from the active HardwareProfile - on a
// V1.2 board the IRQ/RESET remap is UNVERIFIED upstream; if init fails,
// try the V1_1 profile first (see docs/hardware-risk-register.md item 1).

#include "LoRaGateway.h"

#if USE_LORA_DRIVER
#include <SPI.h>
#include <RadioLib.h>
#include <ArduinoJson.h>

// Defaults = Elecrow Lesson13 values. Override any of these in
// config/Pins.h (copy from Pins.example.h). 915 MHz suits US/AU; EU boards
// must use 868.0 - transmitting on the wrong band is a regulatory issue.
#ifndef FIELDOPS_LORA_FREQ_MHZ
#define FIELDOPS_LORA_FREQ_MHZ 915.0
#endif
#ifndef FIELDOPS_LORA_BW_KHZ
#define FIELDOPS_LORA_BW_KHZ 125.0
#endif
#ifndef FIELDOPS_LORA_SF
#define FIELDOPS_LORA_SF 7
#endif
#ifndef FIELDOPS_LORA_CR
#define FIELDOPS_LORA_CR 7
#endif
#ifndef FIELDOPS_LORA_SYNC_WORD
#define FIELDOPS_LORA_SYNC_WORD RADIOLIB_SX126X_SYNC_WORD_PRIVATE
#endif
#ifndef FIELDOPS_LORA_POWER_DBM
#define FIELDOPS_LORA_POWER_DBM 22
#endif
#ifndef FIELDOPS_LORA_PREAMBLE
#define FIELDOPS_LORA_PREAMBLE 8
#endif
#ifndef FIELDOPS_LORA_TCXO_V
#define FIELDOPS_LORA_TCXO_V 1.6
#endif

namespace {
// File-static: one radio per gateway sketch.
Module *radioModule = nullptr;
SX1262 *radio = nullptr;
bool radioReady = false;
volatile bool packetFlag = false;

void IRAM_ATTR onPacketReceived() {
  packetFlag = true;
}

// Expected TX payload (see README): a JSON object like
//   {"node":"node-3","tempC":24.5,"humidity":40.1,"battery":88.0,"motion":false}
// Anything unparseable still surfaces as a packet with nodeId "raw" so
// bring-up against Elecrow's plain-text Lesson13 TX shows life on screen.
bool parsePayload(const String &payload, SensorPacket &packet) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err != DeserializationError::Ok || !doc.is<JsonObject>()) {
    packet.nodeId = "raw";
    Logger::warn("lora", "non-JSON payload: " + payload);
    return true;
  }
  packet.nodeId = doc["node"] | "unknown";
  packet.temperatureC = doc["tempC"] | 0.0f;
  packet.humidityPct = doc["humidity"] | 0.0f;
  packet.batteryPct = doc["battery"] | 0.0f;
  packet.motion = doc["motion"] | false;
  return true;
}
}  // namespace
#endif

void LoRaGateway::begin(const HardwareProfile &profile) {
  profile_ = &profile;
#if USE_LORA_DRIVER
  const WirelessPins &w = profile.wireless;
  SPI.begin(w.spiClk, w.spiMiso, w.spiMosi, w.sx1262Nss);
  radioModule = new Module(w.sx1262Nss, w.sx1262Irq, w.sx1262Reset, w.sx1262Busy, SPI);
  radio = new SX1262(radioModule);

  int16_t state = radio->begin(FIELDOPS_LORA_FREQ_MHZ, FIELDOPS_LORA_BW_KHZ,
                               FIELDOPS_LORA_SF, FIELDOPS_LORA_CR,
                               FIELDOPS_LORA_SYNC_WORD, FIELDOPS_LORA_POWER_DBM,
                               FIELDOPS_LORA_PREAMBLE, FIELDOPS_LORA_TCXO_V);
  if (state != RADIOLIB_ERR_NONE) {
    Logger::error("lora", "SX1262 init failed, RadioLib code " + String(state) +
                              " (wrong board revision profile? see hardware-risk-register.md)");
    return;
  }
  radio->setPacketReceivedAction(onPacketReceived);
  state = radio->startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Logger::error("lora", "startReceive failed, RadioLib code " + String(state));
    return;
  }
  radioReady = true;
  Logger::info("lora", "SX1262 listening on " + String(FIELDOPS_LORA_FREQ_MHZ, 1) + " MHz");
#else
  Logger::info("lora", "real LoRa driver disabled; using base placeholder");
#endif
}

bool LoRaGateway::poll(SensorPacket &packet) {
#if USE_LORA_DRIVER
  if (!radioReady || !packetFlag) {
    return false;
  }
  packetFlag = false;
  String payload;
  int16_t state = radio->readData(payload);
  int rssi = (int)radio->getRSSI();
  radio->startReceive();  // rearm for the next packet
  if (state != RADIOLIB_ERR_NONE) {
    Logger::warn("lora", "readData failed, RadioLib code " + String(state));
    return false;
  }
  if (!parsePayload(payload, packet)) {
    return false;
  }
  packet.rssi = rssi;
  packet.receivedAtMs = millis();
  Logger::info("lora", "packet from " + packet.nodeId + " rssi=" + String(rssi));
  return true;
#else
  (void)packet;
  return false;
#endif
}

const char *LoRaGateway::driverName() const {
#if USE_LORA_DRIVER
  return "sx1262-radiolib";
#else
  return "lora-disabled";
#endif
}
