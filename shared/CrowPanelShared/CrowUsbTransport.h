#ifndef CROW_HID_USB_TRANSPORT_H
#define CROW_HID_USB_TRANSPORT_H

#include "AppConfig.h"
#include "CrowHidTransport.h"

// Live native USB HID requires a USB-OTG build (ARDUINO_USB_MODE==0, i.e. an
// USBMode=default FQBN). Under the suite default USBMode=hwcdc the P4's native
// USB is CDC/JTAG only, so the transport transparently falls back to MOCK.
#if USE_USB_HID && defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#define CROW_HID_USB_LIVE 1
#elif USE_USB_HID
#define CROW_HID_USB_LIVE 0
#warning "USE_USB_HID=1 but this is not an USBMode=default build (ARDUINO_USB_MODE!=0): native USB HID falls back to MOCK. Build with an USBMode=default FQBN for live USB HID."
#else
#define CROW_HID_USB_LIVE 0
#endif

class UsbTransport : public HidTransport {
 public:
  void begin() override;
  bool ready() const override { return CROW_HID_USB_LIVE; }
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
