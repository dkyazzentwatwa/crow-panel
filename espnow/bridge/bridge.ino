// CrowPanel ESP-NOW <-> UART bridge
// ==================================
// Runs on a PLAIN ESP32 (native WiFi radio). The CrowPanel ESP32-P4 cannot be
// an ESP-NOW peer (its WiFi is remote on the C6), so this board joins the
// cypher-chat mesh and forwards what it hears to the panel over UART as CSV:
//
//   SENSOR,<name>,<tempC>,<hum>,<batt>,<motion0/1>,<rssi>
//   PRESENCE,<name>,<rssi>,<type>
//
// It reuses cypher-chat's real MeshManager (vendored under espnow/shared-mesh)
// so it is wire-compatible with existing chat + sensor nodes, including the
// HMAC/compat and AES-GCM modes - MeshManager hands us already-verified,
// already-decrypted payloads.
//
// Build: arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
//          --library espnow/shared-mesh espnow/bridge
// Wiring to the panel: bridge TX -> panel RX, bridge RX -> panel TX, GND<->GND.

#include <Arduino.h>
#include "MeshManager.h"
#include "Config.h"
#include "SensorBeacon.h"

// --- Bridge config (edit to taste) ---
#define BRIDGE_UNIT_NAME "CROWPANEL-GW"
#define BRIDGE_PASSPHRASE DEFAULT_PASSPHRASE  // must match your mesh (default "123456")
#define BRIDGE_UART_TX 17                     // -> panel RX
#define BRIDGE_UART_RX 16                     // <- panel TX
#define BRIDGE_UART_BAUD 115200

HardwareSerial PanelLink(1);  // UART1 to the CrowPanel

static void copyName(char *dst, const char *src, size_t n) {
  size_t i = 0;
  for (; i < n - 1 && src[i]; i++) {
    char c = src[i];
    dst[i] = (c == ',' || c == '\n' || c == '\r') ? ' ' : c;  // keep CSV intact
  }
  dst[i] = '\0';
}

// A decoded mesh message (already verified/decrypted by MeshManager).
void onMeshMessage(const MeshPacket *pkt, const uint8_t *senderMac, int8_t rssi) {
  (void)senderMac;
  if (pkt->header.type == MESH_MSG_DATA &&
      sensorBeaconValid(pkt->payload, pkt->header.payloadLen)) {
    const SensorBeacon *b = reinterpret_cast<const SensorBeacon *>(pkt->payload);
    char name[17];
    copyName(name, b->name, sizeof(name));
    PanelLink.printf("SENSOR,%s,%.1f,%.1f,%.1f,%d,%d\n", name, b->tempC, b->humidityPct,
                     b->batteryPct, b->motion ? 1 : 0, (int)rssi);
    Serial.printf("[bridge] SENSOR %s temp=%.1f batt=%.1f rssi=%d\n", name, b->tempC,
                  b->batteryPct, (int)rssi);
  }
  // Non-sensor DATA (chat text) is surfaced as presence via onMeshPeer below.
}

// Peer discovered / refreshed (heartbeats carry the unit name).
void onMeshPeer(const MeshPeerInfo *peer, bool isNew) {
  (void)isNew;
  char name[17];
  copyName(name, (peer->unitName[0] ? peer->unitName : "node"), sizeof(name));
  PanelLink.printf("PRESENCE,%s,%d,mesh\n", name, (int)peer->rssi);
  Serial.printf("[bridge] PRESENCE %s rssi=%d\n", name, (int)peer->rssi);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  PanelLink.begin(BRIDGE_UART_BAUD, SERIAL_8N1, BRIDGE_UART_RX, BRIDGE_UART_TX);
  Serial.println("CrowPanel ESP-NOW bridge");

  meshMgr.onMessage(onMeshMessage);
  meshMgr.onPeerUpdate(onMeshPeer);
  if (!meshMgr.begin(BRIDGE_UNIT_NAME, BRIDGE_PASSPHRASE)) {
    Serial.println("[bridge] mesh begin FAILED");
  } else {
    Serial.println("[bridge] mesh up on channel 1; forwarding to panel over UART1");
  }
}

void loop() {
  meshMgr.update();
  delay(2);
}
