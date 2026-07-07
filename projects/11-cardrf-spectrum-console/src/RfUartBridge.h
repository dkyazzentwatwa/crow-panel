#ifndef CARDRF_RF_UART_BRIDGE_H
#define CARDRF_RF_UART_BRIDGE_H

#include <Arduino.h>
#include "../config/ProjectConfig.h"

#ifndef CARDRF_BRIDGE_UART_RX
#define CARDRF_BRIDGE_UART_RX -1
#endif

#ifndef CARDRF_BRIDGE_UART_TX
#define CARDRF_BRIDGE_UART_TX -1
#endif

#ifndef CARDRF_BRIDGE_UART_BAUD
#define CARDRF_BRIDGE_UART_BAUD 115200
#endif

class RfUartBridge {
 public:
  void begin();
  bool poll(String &line);
  String status() const;

 private:
  static const uint8_t kMaxLine = 192;

  char line_[kMaxLine] = {0};
  uint8_t lineLen_ = 0;
  bool droppedOverflow_ = false;
};

#endif
