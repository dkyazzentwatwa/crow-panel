#ifndef CYPHER_KEYS_BLE_TRANSPORT_H
#define CYPHER_KEYS_BLE_TRANSPORT_H

#include "../config/ProjectConfig.h"
#include "HidTransport.h"

class BleTransport : public HidTransport {
 public:
  void begin() override;
  bool ready() const override;  // true only while a host is connected
  const char *name() const override { return "BLE"; }
  bool advertising() const;     // stack up and advertising (may be unpaired)
  void clearBonds();            // erase pairings so a host can re-pair

  void keyDown(uint8_t mods, uint8_t key) override;
  void keyUp() override;
  void consumerDown(uint16_t usage) override;
  void consumerUp() override;
  void mouseMove(int8_t dx, int8_t dy) override;
  void mouseDown(uint8_t button) override;
  void mouseUp(uint8_t button) override;
  void mouseWheel(int8_t wheel) override;

 private:
  uint8_t mouseButtons_ = 0;    // held button bitmask for report assembly
};

#endif
