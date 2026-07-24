#ifndef CYPHER_KEYS_USB_TRANSPORT_H
#define CYPHER_KEYS_USB_TRANSPORT_H

#include "../config/ProjectConfig.h"
#include "HidTransport.h"

#if USE_USB_HID && defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#define CYPHER_KEYS_USB_LIVE 1
#else
#define CYPHER_KEYS_USB_LIVE 0
#endif

class UsbTransport : public HidTransport {
 public:
  void begin() override;
  bool ready() const override { return CYPHER_KEYS_USB_LIVE; }
  const char *name() const override { return "USB"; }
  void keyDown(uint8_t mods, uint8_t key) override;
  void keyUp() override;
  void consumerDown(uint16_t usage) override;
  void consumerUp() override;
  void mouseMove(int8_t dx, int8_t dy) override;
  void mouseDown(uint8_t button) override;
  void mouseUp(uint8_t button) override;
  void mouseWheel(int8_t wheel) override;
};

#endif
