#ifndef CROW_HID_BLE_TRANSPORT_H
#define CROW_HID_BLE_TRANSPORT_H

#include "AppConfig.h"
#include "CrowHidTransport.h"

// BLE HID peripheral over the onboard ESP32-C6 (NimBLE host on the P4,
// esp_hosted VHCI to the C6 radio). Gated by USE_BLE_HID; a mock no-op when off.
class BleTransport : public HidTransport {
 public:
  // The advertised device name shown in the host's Bluetooth list. Call before
  // begin(); ignored afterwards. Defaults to "CrowPanel HID".
  void setDeviceName(const char *name);

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
  const char *deviceName_ = "CrowPanel HID";
  uint8_t mouseButtons_ = 0;    // held button bitmask for report assembly
};

#endif
