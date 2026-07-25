#include "CoProcLink.h"

#if USE_STARBEAM_COPROC
#include <HardwareSerial.h>

namespace {
HardwareSerial g_uart(STARBEAM_COPROC_UART_NUM);
constexpr uint32_t kLinkTimeoutMs = 4000;
constexpr uint32_t kPingIntervalMs = 2000;

// pull a signed/unsigned token like "frames=123" out of a line
long tokenValue(const String &line, const char *key, bool &found) {
  int at = line.indexOf(key);
  found = at >= 0;
  if (!found) return 0;
  return line.substring(at + strlen(key)).toInt();
}
}

void CoProcLink::begin() {
  g_uart.begin(STARBEAM_COPROC_BAUD, SERIAL_8N1, STARBEAM_COPROC_RX, STARBEAM_COPROC_TX);
}

void CoProcLink::send(const char *command) {
  g_uart.print(command);
  g_uart.print('\n');
}

void CoProcLink::parseLine_(const String &line, CoProcSnapshot &snap) {
  bool f = false;
  long v;
  v = tokenValue(line, "frames=", f);   if (f) snap.frames = v;
  v = tokenValue(line, "clients=", f);  if (f) snap.clients = v;
  v = tokenValue(line, "captures=", f); if (f) snap.captures = v;
  v = tokenValue(line, "net=", f);      if (f) snap.wifiNetworks = v;
  v = tokenValue(line, "rssi=", f);     if (f) snap.wifiStrongest = v;
  v = tokenValue(line, "ble=", f);      if (f) snap.bleDevices = v;
  // heatmap: "ch=1:20,6:80,11:40"
  int at = line.indexOf("ch=");
  if (at >= 0) {
    String rest = line.substring(at + 3);
    int start = 0;
    while (start < (int)rest.length()) {
      int comma = rest.indexOf(',', start);
      String pair = comma < 0 ? rest.substring(start) : rest.substring(start, comma);
      int colon = pair.indexOf(':');
      if (colon > 0) {
        int ch = pair.substring(0, colon).toInt();
        int util = pair.substring(colon + 1).toInt();
        if (ch >= 1 && ch <= 14) snap.wifiChannels[ch - 1] = constrain(util, 0, 100);
      }
      if (comma < 0) break;
      start = comma + 1;
    }
  }
  // keep the freshest human line as status text
  line.toCharArray(snap.status, sizeof(snap.status));
}

void CoProcLink::poll(CoProcSnapshot &snap) {
  uint32_t now = millis();
  while (g_uart.available()) {
    char c = (char)g_uart.read();
    if (c == '\n' || c == '\r') {
      if (rx_.length() > 0) {
        parseLine_(rx_, snap);
        snap.lastRxMs = now;
        rx_ = "";
      }
    } else if (rx_.length() < 120) {
      rx_ += c;
    }
  }
  if (now - lastPingMs_ >= kPingIntervalMs) {
    lastPingMs_ = now;
    send("status");  // stock starbeam_v2 answers; keeps the link marked live
  }
  linked_ = (now - snap.lastRxMs) < kLinkTimeoutMs;
  snap.linked = linked_;
  if (!linked_) strncpy(snap.status, "offline", sizeof(snap.status));
}

#else  // ---- stub ----

void CoProcLink::begin() {}
void CoProcLink::send(const char *) {}
void CoProcLink::poll(CoProcSnapshot &snap) {
  snap.linked = false;
  linked_ = false;
}

#endif  // USE_STARBEAM_COPROC
