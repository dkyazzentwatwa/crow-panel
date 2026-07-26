#ifndef CROW_HID_TRANSPORT_H
#define CROW_HID_TRANSPORT_H

#include <Arduino.h>

// One HID output path (USB or BLE). CrowHidBackend builds semantic actions and
// routes them to the active transport. `key` bytes are the same space the rest
// of the app uses: ASCII (<0x80) or the kKey*/Arduino KEY_* constants (>=0x80).
// CrowUsbTransport consumes them via the Arduino Keyboard API; CrowBleTransport
// translates them to raw HID usages. Mouse buttons are the MOUSE_LEFT/RIGHT
// bitmask (1=left, 2=right).
class HidTransport {
 public:
  virtual ~HidTransport() {}
  virtual void begin() {}
  virtual bool ready() const = 0;  // can it deliver a report right now?
  virtual const char *name() const = 0;

  virtual void keyDown(uint8_t mods, uint8_t key) = 0;  // hold mods+key
  virtual void keyUp() = 0;                             // release all keys

  virtual void consumerDown(uint16_t usage) = 0;
  virtual void consumerUp() = 0;

  virtual void mouseMove(int8_t dx, int8_t dy) = 0;
  virtual void mouseDown(uint8_t button) = 0;
  virtual void mouseUp(uint8_t button) = 0;
  virtual void mouseWheel(int8_t wheel) = 0;
};

#endif
