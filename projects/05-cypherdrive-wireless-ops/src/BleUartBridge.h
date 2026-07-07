#ifndef CYPHERDRIVE_BLE_UART_BRIDGE_H
#define CYPHERDRIVE_BLE_UART_BRIDGE_H

#include "WirelessTypes.h"

class BleUartBridge {
 public:
  void begin(Stream *input);
  uint8_t scan(BleAdvertisementRecord records[], uint8_t maxRecords, Stream &out);
  uint8_t readAvailable(BleAdvertisementRecord records[], uint8_t maxRecords, Stream &out);
  bool injectLine(const String &line, BleAdvertisementRecord &record, Stream &out);
  const char *driverName() const;

 private:
  bool parseFrame(const String &frame, BleAdvertisementRecord &record, Stream &out) const;
  void printRecord(const BleAdvertisementRecord &record, Stream &out) const;

  Stream *input_ = nullptr;
  char line_[CYPHERDRIVE_BLE_UART_MAX_LINE] = {0};
  uint8_t lineLen_ = 0;
};

#endif
