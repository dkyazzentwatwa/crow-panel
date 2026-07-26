#ifndef CYPHERDRIVE_SD_STORE_H
#define CYPHERDRIVE_SD_STORE_H

#include "../config/ProjectConfig.h"
#include "WirelessTypes.h"
#include <Arduino.h>

// SD_MMC-backed export + payload storage for the field tool. Behind
// USE_CYPHERDRIVE_SD (default off): a card-free build compiles and every call is
// a logged no-op. Uses the CrowPanel's native SDIO slot (mount "/sdcard",
// conservative 1-bit bus). All FS paths are relative to the mount point (no
// "/sdcard" prefix - see CLAUDE.md, the FS-vs-stdio path gotcha).
//
// Layout on the card:
//   /cypherdrive/wifi.csv        appended Wi-Fi finding rows
//   /cypherdrive/ble.csv         appended BLE device rows
//   /cypherdrive/recon.csv       appended recon-tool findings
//   /cypherdrive/payloads/*.txt  DuckyScript payloads for the HID runner
class SdStore {
 public:
  static const uint8_t kMaxPayloads = 16;

  bool begin();
  bool ready() const { return ready_; }
  const char *statusLine() const;  // "SD 3.7 GB free" / "no card"

  // Append one finding to the matching CSV (header written on first row).
  bool appendWifi(const WifiNetworkRecord &net);
  bool appendBle(const BleDeviceRecord &dev);
  bool appendRecon(const String &category, const String &summary, const String &detail);

  // Payloads for the HID runner. listPayloads fills base names (no path);
  // readPayload loads a payload's text. Both return 0/false with no card.
  uint8_t listPayloads(String out[], uint8_t maxCount);
  bool readPayload(const String &name, String &out);

 private:
  bool ready_ = false;
  char status_[32] = "no card";
  bool appendCsv(const char *path, const char *header, const String &row);
};

#endif
