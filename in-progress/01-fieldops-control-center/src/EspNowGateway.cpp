#include "EspNowGateway.h"
#include <CrowPanelShared.h>

// UART pins for the bridge link. Override in config/Pins.h (gitignored) after
// confirming which CrowPanel GPIO-header pins are free (must NOT clash with
// DSI backlight/reset 31/41, touch I2C 45/46/42/40, or the wireless socket).
#ifndef ESPNOW_UART_RX
#define ESPNOW_UART_RX -1
#endif
#ifndef ESPNOW_UART_TX
#define ESPNOW_UART_TX -1
#endif
#ifndef ESPNOW_UART_BAUD
#define ESPNOW_UART_BAUD 115200
#endif

void EspNowGateway::begin(const HardwareProfile &profile) {
  profile_ = &profile;
  Serial1.begin(ESPNOW_UART_BAUD, SERIAL_8N1, ESPNOW_UART_RX, ESPNOW_UART_TX);
  if (ESPNOW_UART_RX < 0 || ESPNOW_UART_TX < 0) {
    Logger::warn("espnow", "UART pins unset; define ESPNOW_UART_RX/TX in config/Pins.h. Using Serial1 defaults.");
  } else {
    Logger::info("espnow", "bridge UART ready rx=" + String(ESPNOW_UART_RX) +
                               " tx=" + String(ESPNOW_UART_TX) + " @" + String(ESPNOW_UART_BAUD));
  }
}

bool EspNowGateway::poll(SensorPacket &packet) {
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (lineLen_ == 0) {
        continue;
      }
      line_[lineLen_] = '\0';
      String frame(line_);
      lineLen_ = 0;
      if (SensorNode::parseCsvFrame(frame, packet)) {
        // One packet per poll(); any remaining bytes are read next loop pass.
        return true;
      }
      continue;  // malformed line: keep draining
    }
    if (lineLen_ >= kMaxLine - 1) {
      lineLen_ = 0;  // overflow: drop the oversized line
      continue;
    }
    line_[lineLen_++] = c;
  }
  return false;
}

const char *EspNowGateway::driverName() const {
  return "espnow-uart-bridge";
}
