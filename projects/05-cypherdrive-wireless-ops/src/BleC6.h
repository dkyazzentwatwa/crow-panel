#ifndef CYPHERDRIVE_BLE_C6_H
#define CYPHERDRIVE_BLE_C6_H

#include "WirelessTypes.h"

// On-panel BLE central through the onboard ESP32-C6 (NimBLE host on the P4,
// esp_hosted VHCI to the C6 radio). Replaces the old external UART BLE sidecar:
// the panel now does its own active BLE scan and GATT interrogation.
//
// IMPORTANT capability caveat: no project in this suite has yet proven C6 hosted
// NimBLE *central* (scan/connect) on real hardware - only BLE *peripheral* (HID,
// project 21). The installed P4 Arduino profile may not enable the esp_hosted
// NimBLE central features. So this is mock-first behind USE_BLE_C6 with a runtime
// available() probe: if the stack does not come up, the tool degrades to a clear
// "BLE central unavailable" state instead of crashing. See TECHNICAL.md.
//
// Read-only interrogation: active scan, connect, enumerate services. No writes,
// no spoofing.
class BleC6 {
 public:
  void begin();

  // Runtime probe: did the NimBLE central stack initialize? Always false in a
  // mock build (there is no stack), true in a mock build's *simulated* form so
  // the UI shows data - see driverName()/hardwareEnabled() to tell them apart.
  bool available() const { return available_; }
  bool hardwareEnabled() const;
  const char *driverName() const;

  // Active scan (requests scan responses, so named devices resolve). Fills
  // records strongest-first, returns count.
  uint8_t scan(BleDeviceRecord records[], uint8_t maxRecords, Stream &out);

  // Connect to a device and enumerate its GATT services. Non-destructive.
  bool connect(const BleDeviceRecord &device, Stream &out);
  void disconnect(Stream &out);
  bool connected() const { return connected_; }
  const String &connectedTo() const { return connectedAddr_; }

  // Services enumerated by the last successful connect(). Returns count copied.
  uint8_t services(BleServiceRecord out_[], uint8_t maxRecords, Stream &out);

 private:
  bool available_ = false;
  bool connected_ = false;
  String connectedAddr_;

  static const uint8_t kMaxServices = 8;
  BleServiceRecord services_[kMaxServices];
  uint8_t serviceCount_ = 0;
};

#endif
