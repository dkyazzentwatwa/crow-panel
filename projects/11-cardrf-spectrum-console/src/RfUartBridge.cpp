#include "RfUartBridge.h"

#include <CrowPanelShared.h>

void RfUartBridge::begin() {
#if USE_RF_UART_BRIDGE
  Serial1.begin(CARDRF_BRIDGE_UART_BAUD, SERIAL_8N1, CARDRF_BRIDGE_UART_RX, CARDRF_BRIDGE_UART_TX);
  if (CARDRF_BRIDGE_UART_RX < 0 || CARDRF_BRIDGE_UART_TX < 0) {
    Logger::warn("cardrf-bridge", "UART pins unset; define CARDRF_BRIDGE_UART_RX/TX after hardware proof");
  } else {
    Logger::info("cardrf-bridge", String("UART RX bridge ready rx=") +
                                      String(CARDRF_BRIDGE_UART_RX) + " tx=" +
                                      String(CARDRF_BRIDGE_UART_TX) + " @" +
                                      String(CARDRF_BRIDGE_UART_BAUD));
  }
#else
  Logger::info("cardrf-bridge", "mock-only; build with USE_RF_UART_BRIDGE=1 to ingest Serial1 lines");
#endif
}

bool RfUartBridge::poll(String &line) {
  line = "";
#if USE_RF_UART_BRIDGE
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (droppedOverflow_) {
        droppedOverflow_ = false;
      } else if (lineLen_ > 0) {
        line_[lineLen_] = '\0';
        line = String(line_);
        lineLen_ = 0;
        return true;
      }
      lineLen_ = 0;
      continue;
    }
    if (droppedOverflow_) {
      continue;
    }
    if (lineLen_ >= kMaxLine - 1) {
      droppedOverflow_ = true;
      lineLen_ = 0;
      Logger::warn("cardrf-bridge", "UART line too long; dropped");
      continue;
    }
    line_[lineLen_++] = c;
  }
#endif
  return false;
}

String RfUartBridge::status() const {
#if USE_RF_UART_BRIDGE
  return String("enabled baud=") + String(CARDRF_BRIDGE_UART_BAUD) + " rx=" +
         String(CARDRF_BRIDGE_UART_RX) + " tx=" + String(CARDRF_BRIDGE_UART_TX);
#else
  return "disabled mock-only";
#endif
}
