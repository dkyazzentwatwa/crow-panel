#ifndef CROW_HID_GAMEPAD_TRANSPORT_H
#define CROW_HID_GAMEPAD_TRANSPORT_H

#include "AppConfig.h"

#include <Arduino.h>

// Native USB gamepad output (project 23, Cypher Stick).
//
// This is NOT a HidTransport: that interface is keyboard/consumer/mouse-shaped
// and a gamepad implements none of it. HidBackend routes to this class
// directly via gamepadState().
//
// The one rule that matters here: emit whole state with a single send(). The
// core's USBHIDGamepad::pressButton()/releaseButton()/hat() each write their
// own USB report, so pressing three buttons would cost three reports and three
// frames of skew. sendState() writes exactly one report for the whole stick.
#if USE_USB_GAMEPAD && defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#define CROW_GAMEPAD_USB_LIVE 1
#elif USE_USB_GAMEPAD
#define CROW_GAMEPAD_USB_LIVE 0
#warning "USE_USB_GAMEPAD=1 but this is not an USBMode=default build (ARDUINO_USB_MODE!=0): native USB gamepad falls back to MOCK. Build with an USBMode=default FQBN for a live gamepad."
#else
#define CROW_GAMEPAD_USB_LIVE 0
#endif

class GamepadTransport {
 public:
  void begin();
  bool ready() const { return CROW_GAMEPAD_USB_LIVE; }
  const char *name() const { return CROW_GAMEPAD_USB_LIVE ? "PAD" : "PAD-MOCK"; }

  // One atomic HID report: hat (0-8) plus a 32-bit button mask.
  void sendState(uint8_t hat, uint32_t buttons);

  uint32_t reports() const { return reports_; }

 private:
  uint32_t reports_ = 0;
  bool begun_ = false;
};

#endif
