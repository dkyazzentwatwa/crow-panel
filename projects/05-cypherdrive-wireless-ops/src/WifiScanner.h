#ifndef CYPHERDRIVE_WIFI_SCANNER_H
#define CYPHERDRIVE_WIFI_SCANNER_H

#include "WirelessTypes.h"

class WifiScanner {
 public:
  void begin();
  uint8_t scan(WifiNetworkRecord records[], uint8_t maxRecords, Stream &out);
  const char *driverName() const;
  bool hardwareEnabled() const;
};

#endif
