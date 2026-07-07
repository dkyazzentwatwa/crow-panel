#include "MeshManager.h"
#include "MeshCrypto.h"
#include "MessageAuth.h"
#include "Config.h"
#include "OutputManager.h"
#include <cstring>
#include <algorithm>
#include <esp_wifi.h>
#include <esp_random.h>

static constexpr uint8_t COMPAT_MESH_PROTOCOL_VERSION = 0x01;
static constexpr size_t COMPAT_HMAC_SIZE = 8;
static constexpr bool MESH_DEFAULT_COMPAT_MODE = true;

// ============================================================================
// MeshPeer Implementation
// ============================================================================

void MeshPeer::onReceive(const uint8_t *data, size_t len, bool broadcast) {
  (void)broadcast;
  if (_manager) {
    _manager->handlePeerReceive(addr(), data, len);
  }
}

void MeshPeer::onSent(bool success) {
  if (_manager) {
    _manager->handlePeerSent(addr(), success);
  }
}

// ============================================================================
// MeshManager Implementation
// ============================================================================

MeshManager meshMgr;

// Broadcast MAC address
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Callback for ESP-NOW new peer packets (declared as friend in MeshManager.h)
void onNewPeerPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  MeshManager *manager = static_cast<MeshManager*>(arg);
  if (manager && info && len > 0) {
    manager->handlePeerReceive(info->src_addr, data, (size_t)len);
  }
}

static uint64_t macToKey(const uint8_t* mac) {
  uint64_t key = 0;
  for (int i = 0; i < 6; i++) {
    key = (key << 8) | mac[i];
  }
  return key;
}

MeshManager::MeshManager()
  : _running(false)
  , _messageCallback(nullptr)
  , _peerCallback(nullptr)
  , _seenIndex(0)
  , _lastHeartbeat(0)
  , _lastPeerPrune(0)
  , _msgsSent(0)
  , _msgsReceived(0)
  , _msgsRelayed(0)
  , _msgsDropped(0)
  , _messageIdCounter(0)
  , _broadcastPeer(nullptr)
  , _lastMacRotation(0)
{
  memset(_unitName, 0, sizeof(_unitName));
  memset(_passphrase, 0, sizeof(_passphrase));
  memset(_myMac, 0, sizeof(_myMac));
  memset(_originalMac, 0, sizeof(_originalMac));
  memset(_seenMessages, 0, sizeof(_seenMessages));
  memset(_storeQueue, 0, sizeof(_storeQueue));
  _myGPS = {0, 0, 0, false};
}

void MeshManager::randomizeMac() {
#if MESH_MAC_RANDOMIZE
  uint8_t newMac[6];

  // Generate random bytes using ESP32 hardware RNG
  uint32_t rand1 = esp_random();
  uint32_t rand2 = esp_random();

  newMac[0] = (rand1 >> 0) & 0xFF;
  newMac[1] = (rand1 >> 8) & 0xFF;
  newMac[2] = (rand1 >> 16) & 0xFF;
  newMac[3] = (rand2 >> 0) & 0xFF;
  newMac[4] = (rand2 >> 8) & 0xFF;
  newMac[5] = (rand2 >> 16) & 0xFF;

#if MESH_MAC_KEEP_OUI
  // Keep Espressif OUI prefix (24:0A:C4 or other Espressif prefixes)
  // This is more compatible but less anonymous
  newMac[0] = _originalMac[0];
  newMac[1] = _originalMac[1];
  newMac[2] = _originalMac[2];
#else
  // Set locally-administered bit (bit 1 of first byte = 1)
  // Clear multicast bit (bit 0 of first byte = 0)
  // This marks MAC as locally-administered rather than globally-unique
  newMac[0] = (newMac[0] & 0xFC) | 0x02;
#endif

  // Set the new MAC address
  esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, newMac);
  if (err == ESP_OK) {
    memcpy(_myMac, newMac, 6);
    output.print("MeshManager: MAC randomized to ");
    for (int i = 0; i < 6; i++) {
      if (i > 0) output.print(":");
      output.printf("%02X", newMac[i]);
    }
    output.println();
  } else {
    output.printf("MeshManager: Failed to set random MAC (err=%d)\n", err);
  }
#endif
}

bool MeshManager::begin(const char* unitName, const char* passphrase) {
  if (_running) {
    return true;  // Already running
  }

  // Store configuration
  strncpy(_unitName, unitName, sizeof(_unitName) - 1);
  _unitName[sizeof(_unitName) - 1] = '\0';
  strncpy(_passphrase, passphrase ? passphrase : "", sizeof(_passphrase) - 1);
  _passphrase[sizeof(_passphrase) - 1] = '\0';

  // Initialize crypto with passphrase (HKDF key derivation)
  if (!MeshCrypto::init(passphrase)) {
    output.println("MeshManager: Crypto init failed");
    return false;
  }

  // Clear replay counters on each boot to prevent false replay detection
  // when peers reboot and restart their messageId sequences
  MeshCrypto::clearReplayCounters();

  // Initialize WiFi in station mode (required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Wait for WiFi to start
  unsigned long wifiStart = millis();
  while (!WiFi.STA.started()) {
    if (millis() - wifiStart > MESH_WIFI_START_TIMEOUT_MS) {
      output.println("MeshManager: WiFi STA start timeout");
      return false;
    }
    delay(10);
  }

  // Store original MAC before any randomization
  WiFi.macAddress(_originalMac);

  // Randomize MAC if enabled (anti-tracking)
#if MESH_MAC_RANDOMIZE
  randomizeMac();
  _lastMacRotation = millis();
#endif

  // Set WiFi channel using new API
  WiFi.setChannel(MESH_CHANNEL);

  // Get our (possibly randomized) MAC address
  WiFi.macAddress(_myMac);

  // Initialize ESP-NOW using new class-based API
  if (!ESP_NOW.begin()) {
    output.println("MeshManager: ESP-NOW init failed");
    return false;
  }

  // Create and register broadcast peer for flood messages
  // Broadcast peer cannot use LMK (ESP-NOW limitation)
  _broadcastPeer = new MeshPeer(ESP_NOW.BROADCAST_ADDR, MESH_CHANNEL, WIFI_IF_STA, nullptr);
  _broadcastPeer->setManager(this);
  if (!_broadcastPeer->begin()) {
    output.println("MeshManager: Failed to register broadcast peer");
    delete _broadcastPeer;
    _broadcastPeer = nullptr;
    ESP_NOW.end();
    return false;
  }

  // Register global receive callback to catch ALL ESP-NOW packets
  ESP_NOW.onNewPeer(onNewPeerPacket, this);

  // Generate initial message ID from MAC + time
  _messageIdCounter = (_myMac[4] << 24) | (_myMac[5] << 16) | (millis() & 0xFFFF);

  _running = true;
  _lastHeartbeat = millis();
  _lastPeerPrune = millis();

  output.print("MeshManager: Started on channel ");
  output.print(MESH_CHANNEL);
  output.print(", MAC: ");
  for (int i = 0; i < 6; i++) {
    if (i > 0) output.print(":");
    output.printf("%02X", _myMac[i]);
  }
  output.println();

  output.printf("Security: AES-256-GCM AEAD + HKDF-SHA256 + replay protection\n");
#if MESH_MAC_RANDOMIZE
  output.printf("Anti-tracking: MAC randomization enabled");
#if MESH_MAC_ROTATE_MS > 0
  output.printf(" (rotating every %d min)", MESH_MAC_ROTATE_MS / 60000);
#else
  output.printf(" (boot-only)");
#endif
  output.println();
#endif

  // Send initial heartbeat to announce presence
  sendHeartbeat();

  return true;
}

void MeshManager::end() {
  if (!_running) return;

  // Save replay counters before shutdown
  MeshCrypto::saveReplayCounters();

  // Delete broadcast peer
  if (_broadcastPeer) {
    delete _broadcastPeer;
    _broadcastPeer = nullptr;
  }

  // Delete all mesh peers
  for (auto& pair : _peers) {
    delete pair.second;
  }
  _peers.clear();
  _peerInfo.clear();

  // Shutdown ESP-NOW
  ESP_NOW.end();

  _running = false;

  output.println("MeshManager: Stopped");
}

MeshPeer* MeshManager::findOrCreatePeer(const uint8_t* mac) {
  if (!mac) return nullptr;

  uint64_t macKey = macToKey(mac);
  auto it = _peers.find(macKey);
  if (it != _peers.end()) {
    return it->second;
  }

  // Derive per-peer LMK for ESP-NOW link-layer encryption (unicast only)
  uint8_t lmk[MESH_CRYPTO_LMK_SIZE];
  bool hasLmk = MeshCrypto::deriveLMK(mac, lmk);

  MeshPeer* peer = new MeshPeer(mac, MESH_CHANNEL, WIFI_IF_STA, hasLmk ? lmk : nullptr);
  peer->setManager(this);

  if (peer->begin()) {
    _peers[macKey] = peer;
    output.printf("MeshManager: Added peer " MACSTR " (LMK: %s)\n",
                  MAC2STR(mac), hasLmk ? "yes" : "no");
    return peer;
  }

  output.printf("MeshManager: Failed to add peer " MACSTR "\n", MAC2STR(mac));
  delete peer;
  return nullptr;
}

void MeshManager::handlePeerReceive(const uint8_t* senderMac, const uint8_t* data, size_t len) {
  if (!_running || !senderMac || !data || len < sizeof(MeshHeader)) {
    return;
  }

  const MeshHeader* header = reinterpret_cast<const MeshHeader*>(data);
  if (header->version == COMPAT_MESH_PROTOCOL_VERSION) {
    if (header->payloadLen > MESH_MAX_PAYLOAD - COMPAT_HMAC_SIZE) {
      _msgsDropped++;
      output.printf("MeshManager: Drop invalid compat packet (payloadLen=%u)\n", header->payloadLen);
      return;
    }

    size_t expectedCompatLen = sizeof(MeshHeader) + header->payloadLen + COMPAT_HMAC_SIZE;
    if (len != expectedCompatLen || expectedCompatLen > sizeof(MeshPacket)) {
      _msgsDropped++;
      output.printf("MeshManager: Drop malformed compat packet (len=%u expected=%u)\n",
                    (unsigned)len, (unsigned)expectedCompatLen);
      return;
    }

    MeshPacket packet;
    memset(&packet, 0, sizeof(packet));
    memcpy(&packet, data, expectedCompatLen);

    int8_t rssi = -50;
    processCompatPacket(&packet, senderMac, rssi);
    return;
  }

  if (header->version != MESH_PROTOCOL_VERSION) {
    _msgsDropped++;
    output.printf("MeshManager: Drop unsupported packet version 0x%02X\n", header->version);
    return;
  }

  if (header->payloadLen > MESH_MAX_PLAINTEXT) {
    _msgsDropped++;
    output.printf("MeshManager: Drop invalid packet (payloadLen=%u)\n", header->payloadLen);
    return;
  }

  size_t expectedLen = sizeof(MeshHeader) + MESH_CRYPTO_NONCE_SIZE + header->payloadLen + MESH_CRYPTO_TAG_SIZE;
  if (len != expectedLen || expectedLen > sizeof(MeshPacket)) {
    _msgsDropped++;
    output.printf("MeshManager: Drop malformed packet (len=%u expected=%u)\n",
                  (unsigned)len, (unsigned)expectedLen);
    return;
  }

  MeshPacket packet;
  memset(&packet, 0, sizeof(packet));
  memcpy(&packet, data, expectedLen);

  // RSSI is not provided via the class-based API callbacks.
  int8_t rssi = -50;
  processPacket(&packet, senderMac, rssi);
}

void MeshManager::handlePeerSent(const uint8_t* peerMac, bool success) {
  if (!success) {
    _msgsDropped++;
    output.printf("MeshManager: Send failed to " MACSTR "\n", MAC2STR(peerMac));
  }
}

void MeshManager::update() {
  if (!_running) return;

  unsigned long now = millis();

  // Periodic MAC rotation for anti-tracking
#if MESH_MAC_RANDOMIZE && (MESH_MAC_ROTATE_MS > 0)
  if (now - _lastMacRotation >= MESH_MAC_ROTATE_MS) {
    output.println("MeshManager: Rotating MAC address for anti-tracking...");

    // Randomize MAC
    randomizeMac();
    _lastMacRotation = now;

    // Re-register broadcast peer with new MAC context
    // (ESP-NOW peers from our old MAC won't work, but we still receive via onNewPeer callback)

    // Send immediate heartbeat to announce new MAC to peers
    // Peers will learn our new MAC + unitName association
    sendHeartbeat();
  }
#endif

  // Send periodic heartbeat
  if (now - _lastHeartbeat >= MESH_HEARTBEAT_MS) {
    sendHeartbeat();
    _lastHeartbeat = now;
  }

  // Prune offline peers periodically
  if (now - _lastPeerPrune >= MESH_PEER_TIMEOUT_MS / 2) {
    pruneOfflinePeers();
    _lastPeerPrune = now;
  }

  // Process store-and-forward queue
  processStoreQueue();
}

static size_t encryptedWireSize(uint8_t payloadLen) {
  return sizeof(MeshHeader) + MESH_CRYPTO_NONCE_SIZE + payloadLen + MESH_CRYPTO_TAG_SIZE;
}

static size_t compatWireSize(uint8_t payloadLen) {
  return sizeof(MeshHeader) + payloadLen + COMPAT_HMAC_SIZE;
}

static size_t packetWireSize(const MeshPacket* packet) {
  if (packet && packet->header.version == COMPAT_MESH_PROTOCOL_VERSION) {
    return compatWireSize(packet->header.payloadLen);
  }
  return encryptedWireSize(packet ? packet->header.payloadLen : 0);
}

uint32_t MeshManager::broadcast(const uint8_t* data, size_t len, MeshMessageType type, uint8_t ttl) {
  size_t maxPayload = MESH_DEFAULT_COMPAT_MODE ? (MESH_MAX_PAYLOAD - COMPAT_HMAC_SIZE) : MESH_MAX_PLAINTEXT;
  if (!_running || data == nullptr || len == 0 || len > maxPayload) {
    return 0;
  }

  MeshPacket packet;
  memset(&packet, 0, sizeof(packet));

  // Fill header
  packet.header.version = MESH_DEFAULT_COMPAT_MODE ? COMPAT_MESH_PROTOCOL_VERSION : MESH_PROTOCOL_VERSION;
  packet.header.type = type;
  packet.header.ttl = ttl;
  packet.header.hopCount = 0;
  packet.header.messageId = generateMessageId();
  memcpy(packet.header.originMac, _myMac, 6);
  memcpy(packet.header.destMac, BROADCAST_MAC, 6);
  memcpy(packet.header.lastHopMac, _myMac, 6);
  packet.header.timestamp = millis();
  packet.header.payloadLen = len;
  packet.header.flags = 0;

  memcpy(packet.payload, data, len);

  size_t totalSize = 0;
  if (MESH_DEFAULT_COMPAT_MODE) {
    if (!signCompatPacket(&packet)) {
      output.println("MeshManager: Failed to sign compat packet");
      return 0;
    }
    totalSize = compatWireSize(packet.header.payloadLen);
  } else {
    if (!MeshCrypto::encryptPayload(&packet)) {
      output.println("MeshManager: Failed to encrypt packet");
      return 0;
    }
    totalSize = encryptedWireSize(packet.header.payloadLen);
  }

  markMessageSeen(packet.header.messageId);

  if (!_broadcastPeer) {
    output.println("MeshManager: ERROR - Broadcast peer is NULL!");
    return 0;
  }

  bool sendResult = _broadcastPeer->sendMessage((uint8_t*)&packet, totalSize);

  if (sendResult) {
    _msgsSent++;
    return packet.header.messageId;
  }

  output.println("MeshManager: Broadcast send failed");
  return 0;
}

uint32_t MeshManager::sendTo(const uint8_t* destMac, const uint8_t* data, size_t len, MeshMessageType type) {
  size_t maxPayload = MESH_DEFAULT_COMPAT_MODE ? (MESH_MAX_PAYLOAD - COMPAT_HMAC_SIZE) : MESH_MAX_PLAINTEXT;
  if (!_running || destMac == nullptr || data == nullptr || len == 0 || len > maxPayload) {
    return 0;
  }

  MeshPacket packet;
  memset(&packet, 0, sizeof(packet));

  // Fill header
  packet.header.version = MESH_DEFAULT_COMPAT_MODE ? COMPAT_MESH_PROTOCOL_VERSION : MESH_PROTOCOL_VERSION;
  packet.header.type = type;
  packet.header.ttl = MESH_DEFAULT_TTL;
  packet.header.hopCount = 0;
  packet.header.messageId = generateMessageId();
  memcpy(packet.header.originMac, _myMac, 6);
  memcpy(packet.header.destMac, destMac, 6);
  memcpy(packet.header.lastHopMac, _myMac, 6);
  packet.header.timestamp = millis();
  packet.header.payloadLen = len;
  packet.header.flags = 0x01;  // Needs ACK

  memcpy(packet.payload, data, len);

  size_t totalSize = 0;
  if (MESH_DEFAULT_COMPAT_MODE) {
    if (!signCompatPacket(&packet)) {
      return 0;
    }
    totalSize = compatWireSize(packet.header.payloadLen);
  } else {
    if (!MeshCrypto::encryptPayload(&packet)) {
      return 0;
    }
    totalSize = encryptedWireSize(packet.header.payloadLen);
  }

  markMessageSeen(packet.header.messageId);

  // Find or create peer
  MeshPeer* peer = findOrCreatePeer(destMac);
  if (peer && peer->sendMessage((uint8_t*)&packet, totalSize)) {
    _msgsSent++;
    return packet.header.messageId;
  }

  // Peer not directly reachable - broadcast for mesh routing
  if (_broadcastPeer && _broadcastPeer->sendMessage((uint8_t*)&packet, totalSize)) {
    _msgsSent++;
    return packet.header.messageId;
  }

  // Store for later delivery if send fails
  storeForDelivery(destMac, data, len);
  return 0;
}

uint32_t MeshManager::sendEmergency(const char* message, const GPSCoordinates* gps) {
  if (!_running || message == nullptr) {
    return 0;
  }

  // Build emergency payload (plaintext, will be encrypted by broadcast())
  // Format: [flags:1][lat:4][lon:4][alt:4][msg:variable]
  uint8_t payload[MESH_MAX_PLAINTEXT];
  size_t offset = 0;

  // Flags byte
  uint8_t flags = 0;
  if (gps && gps->valid) {
    flags |= 0x01;  // Has GPS
  }
  payload[offset++] = flags;

  // GPS coordinates if available
  if (gps && gps->valid) {
    memcpy(&payload[offset], &gps->latitude, 4);
    offset += 4;
    memcpy(&payload[offset], &gps->longitude, 4);
    offset += 4;
    memcpy(&payload[offset], &gps->altitude, 4);
    offset += 4;
  } else if (_myGPS.valid) {
    // Use device's stored GPS
    flags |= 0x01;
    payload[0] = flags;
    memcpy(&payload[offset], &_myGPS.latitude, 4);
    offset += 4;
    memcpy(&payload[offset], &_myGPS.longitude, 4);
    offset += 4;
    memcpy(&payload[offset], &_myGPS.altitude, 4);
    offset += 4;
  }

  // Message content
  size_t msgLen = strlen(message);
  if (offset + msgLen > sizeof(payload)) {
    msgLen = sizeof(payload) - offset;
  }
  memcpy(&payload[offset], message, msgLen);
  offset += msgLen;

  // Send with maximum TTL for emergency
  return broadcast(payload, offset, MESH_MSG_EMERGENCY, MESH_MAX_TTL);
}

bool MeshManager::storeForDelivery(const uint8_t* destMac, const uint8_t* data, size_t len) {
  if (!_running || destMac == nullptr || data == nullptr || len == 0) {
    return false;
  }

  // Find free slot or oldest entry
  int freeSlot = -1;
  uint32_t oldestTime = UINT32_MAX;
  int oldestSlot = 0;

  for (int i = 0; i < MESH_STORE_QUEUE_SIZE; i++) {
    if (!_storeQueue[i].active) {
      freeSlot = i;
      break;
    }
    if (_storeQueue[i].storedTime < oldestTime) {
      oldestTime = _storeQueue[i].storedTime;
      oldestSlot = i;
    }
  }

  int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;

  // Build packet for storage
  MeshPacket packet;
  memset(&packet, 0, sizeof(packet));
  packet.header.version = MESH_DEFAULT_COMPAT_MODE ? COMPAT_MESH_PROTOCOL_VERSION : MESH_PROTOCOL_VERSION;
  packet.header.type = MESH_MSG_FORWARD;
  packet.header.ttl = MESH_DEFAULT_TTL;
  packet.header.hopCount = 0;
  packet.header.messageId = generateMessageId();
  memcpy(packet.header.originMac, _myMac, 6);
  memcpy(packet.header.destMac, destMac, 6);
  memcpy(packet.header.lastHopMac, _myMac, 6);
  packet.header.timestamp = millis();
  packet.header.flags = 0x02;  // Is relay/stored

  size_t maxPayload = MESH_DEFAULT_COMPAT_MODE ? (MESH_MAX_PAYLOAD - COMPAT_HMAC_SIZE) : MESH_MAX_PLAINTEXT;
  if (len > maxPayload) {
    len = maxPayload;
  }
  packet.header.payloadLen = len;
  memcpy(packet.payload, data, len);

  if (MESH_DEFAULT_COMPAT_MODE) {
    signCompatPacket(&packet);
  } else {
    MeshCrypto::encryptPayload(&packet);
  }

  // Store entry
  _storeQueue[slot].packet = packet;
  memcpy(_storeQueue[slot].targetMac, destMac, 6);
  _storeQueue[slot].storedTime = millis();
  _storeQueue[slot].retryCount = 0;
  _storeQueue[slot].active = true;

  output.printf("MeshManager: Stored message for later delivery (%d/%d slots used)\n",
                getStoredMessageCount(), MESH_STORE_QUEUE_SIZE);

  return true;
}

void MeshManager::onMessage(MeshMessageCallback callback) {
  _messageCallback = callback;
}

void MeshManager::onPeerUpdate(MeshPeerCallback callback) {
  _peerCallback = callback;
}

std::vector<MeshPeerInfo> MeshManager::getPeers() const {
  std::vector<MeshPeerInfo> peers;
  peers.reserve(_peerInfo.size());
  for (const auto& entry : _peerInfo) {
    peers.push_back(entry.second);
  }
  return peers;
}

int MeshManager::getOnlinePeerCount() const {
  int count = 0;
  for (const auto& entry : _peerInfo) {
    if (entry.second.isOnline) count++;
  }
  return count;
}

void MeshManager::getMyMac(uint8_t* mac) const {
  if (mac) {
    memcpy(mac, _myMac, 6);
  }
}

void MeshManager::setGPS(const GPSCoordinates& gps) {
  _myGPS = gps;
}

void MeshManager::processPacket(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi) {
  // Verify protocol version
  if (packet->header.version != MESH_PROTOCOL_VERSION) {
    _msgsDropped++;
    return;
  }

  // Check if message already seen (deduplication) - check BEFORE decryption for efficiency
  if (isMessageSeen(packet->header.messageId)) {
    return;
  }

  // Decrypt and authenticate payload (AEAD verification)
  // Make a mutable copy for decryption - keep original for relay
  MeshPacket decrypted = *packet;
  if (!MeshCrypto::decryptPayload(&decrypted)) {
    output.println("MeshManager: AEAD decryption failed - wrong key or tampered");
    _msgsDropped++;
    return;
  }

  // Replay counter check (per-sender monotonic counter)
  if (!MeshCrypto::checkReplayCounter(decrypted.header.originMac, decrypted.header.messageId)) {
    output.println("MeshManager: Replay attack detected - message rejected");
    _msgsDropped++;
    return;
  }

  // Mark as seen
  markMessageSeen(decrypted.header.messageId);

  _msgsReceived++;

  // Update peer info from sender
  updatePeer(senderMac, rssi, nullptr, 1);

  // Update peer info from origin (multi-hop)
  if (memcmp(decrypted.header.originMac, senderMac, 6) != 0) {
    updatePeer(decrypted.header.originMac, -127, nullptr, decrypted.header.hopCount + 1);
  }

  // Handle by message type (using DECRYPTED copy)
  switch (decrypted.header.type) {
    case MESH_MSG_DATA:
    case MESH_MSG_FORWARD:
      handleDataMessage(&decrypted, senderMac, rssi);
      break;

    case MESH_MSG_EMERGENCY:
      handleEmergency(&decrypted, senderMac);
      break;

    case MESH_MSG_ACK:
      handleAck(&decrypted);
      break;

    case MESH_MSG_HEARTBEAT:
      handleHeartbeat(&decrypted, senderMac, rssi);
      break;

    default:
      output.printf("MeshManager: Unknown message type: 0x%02X\n", decrypted.header.type);
      break;
  }

  // Relay using ORIGINAL encrypted packet (no re-encryption needed)
  // Only mutable header fields change (TTL, hopCount, lastHopMac)
  // AEAD tag remains valid because AAD only covers immutable fields
  if (shouldRelay(&decrypted)) {
    MeshPacket relay = *packet;  // Copy of ENCRYPTED original
    relayPacket(&relay);
  }
}

void MeshManager::processCompatPacket(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi) {
  if (!verifyCompatPacket(packet)) {
    output.println("MeshManager: Compat HMAC verification failed");
    _msgsDropped++;
    return;
  }

  if (isMessageSeen(packet->header.messageId)) {
    return;
  }

  markMessageSeen(packet->header.messageId);
  _msgsReceived++;

  updatePeer(senderMac, rssi, nullptr, 1);

  if (memcmp(packet->header.originMac, senderMac, 6) != 0) {
    updatePeer(packet->header.originMac, -127, nullptr, packet->header.hopCount + 1);
  }

  switch (packet->header.type) {
    case MESH_MSG_DATA:
    case MESH_MSG_FORWARD:
      handleDataMessage(packet, senderMac, rssi);
      break;

    case MESH_MSG_EMERGENCY:
      handleEmergency(packet, senderMac);
      break;

    case MESH_MSG_ACK:
      handleAck(packet);
      break;

    case MESH_MSG_HEARTBEAT:
      handleHeartbeat(packet, senderMac, rssi);
      break;

    default:
      output.printf("MeshManager: Unknown compat message type: 0x%02X\n", packet->header.type);
      break;
  }

  if (shouldRelay(packet)) {
    MeshPacket relay = *packet;
    relayPacket(&relay);
  }
}

void MeshManager::handleDataMessage(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi) {
  // Check if message is for us
  bool isForUs = memcmp(packet->header.destMac, _myMac, 6) == 0 ||
                 memcmp(packet->header.destMac, BROADCAST_MAC, 6) == 0;

  if (isForUs) {
    // Deliver to application (decrypted payload)
    if (_messageCallback) {
      _messageCallback(packet, senderMac, rssi);
    }

    // Send ACK if requested
    if (packet->header.flags & 0x01) {
      // Build ACK packet
      MeshPacket ack;
      memset(&ack, 0, sizeof(ack));
      ack.header.version = packet->header.version == COMPAT_MESH_PROTOCOL_VERSION
        ? COMPAT_MESH_PROTOCOL_VERSION
        : MESH_PROTOCOL_VERSION;
      ack.header.type = MESH_MSG_ACK;
      ack.header.ttl = MESH_DEFAULT_TTL;
      ack.header.hopCount = 0;
      ack.header.messageId = generateMessageId();
      memcpy(ack.header.originMac, _myMac, 6);
      memcpy(ack.header.destMac, packet->header.originMac, 6);
      memcpy(ack.header.lastHopMac, _myMac, 6);
      ack.header.timestamp = millis();

      // ACK payload contains original message ID
      memcpy(ack.payload, &packet->header.messageId, 4);
      ack.header.payloadLen = 4;

      size_t ackSize = 0;
      if (ack.header.version == COMPAT_MESH_PROTOCOL_VERSION) {
        signCompatPacket(&ack);
        ackSize = compatWireSize(ack.header.payloadLen);
      } else {
        MeshCrypto::encryptPayload(&ack);
        ackSize = encryptedWireSize(ack.header.payloadLen);
      }

      // Send ACK
      if (_broadcastPeer) {
        _broadcastPeer->sendMessage((uint8_t*)&ack, ackSize);
      }
    }
  }

  // Note: relay is handled in processPacket() after this returns
}

void MeshManager::handleEmergency(const MeshPacket* packet, const uint8_t* senderMac) {
  // Rate limiting: reject if same sender sent emergency within cooldown period
  uint64_t senderKey = macToKey(packet->header.originMac);
  uint32_t now = millis();
  auto it = _emergencyRateLimit.find(senderKey);
  if (it != _emergencyRateLimit.end()) {
    if (now - it->second < MESH_EMERGENCY_COOLDOWN_MS) {
      // Rate limited - silently drop
      return;
    }
  }
  _emergencyRateLimit[senderKey] = now;

  // Emergency messages are always delivered (decrypted payload)
  if (_messageCallback) {
    _messageCallback(packet, senderMac, 0);
  }

  // Note: relay is handled in processPacket() after this returns
}

void MeshManager::handleAck(const MeshPacket* packet) {
  // Check if ACK is for us
  if (memcmp(packet->header.destMac, _myMac, 6) != 0) {
    // Note: relay is handled in processPacket()
    return;
  }

  // Extract original message ID from ACK payload
  if (packet->header.payloadLen >= 4) {
    uint32_t originalMsgId;
    memcpy(&originalMsgId, packet->payload, 4);

    output.printf("MeshManager: ACK received for message %08lX\n",
                  (unsigned long)originalMsgId);

    // Clear from store queue if present
    for (int i = 0; i < MESH_STORE_QUEUE_SIZE; i++) {
      if (_storeQueue[i].active && _storeQueue[i].packet.header.messageId == originalMsgId) {
        _storeQueue[i].active = false;
        break;
      }
    }
  }
}

void MeshManager::handleHeartbeat(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi) {
  // Extract unit name from heartbeat payload (already decrypted)
  char peerName[17] = {0};
  if (packet->header.payloadLen > 0 && packet->header.payloadLen <= 16) {
    memcpy(peerName, packet->payload, packet->header.payloadLen);
    peerName[packet->header.payloadLen] = '\0';
  }

  // Update peer
  updatePeer(senderMac, rssi, peerName, 1);

  // Check if we have stored messages for this peer
  deliverStoredMessages(senderMac);
}

bool MeshManager::shouldRelay(const MeshPacket* packet) {
  // Don't relay if we're the origin
  if (memcmp(packet->header.originMac, _myMac, 6) == 0) {
    return false;
  }

  // Don't relay if TTL exhausted
  if (packet->header.ttl <= 1) {
    return false;
  }

  // Always relay emergencies
  if (packet->header.type == MESH_MSG_EMERGENCY) {
    return true;
  }

  // Relay broadcasts
  if (memcmp(packet->header.destMac, BROADCAST_MAC, 6) == 0) {
    return true;
  }

  // Relay unicast messages not for us
  if (memcmp(packet->header.destMac, _myMac, 6) != 0) {
    return true;
  }

  return false;
}

void MeshManager::relayPacket(MeshPacket* packet) {
  packet->header.ttl--;
  packet->header.hopCount++;
  memcpy(packet->header.lastHopMac, _myMac, 6);

  if (_broadcastPeer &&
      _broadcastPeer->sendMessage((uint8_t*)packet, packetWireSize(packet))) {
    _msgsRelayed++;
  }
}

bool MeshManager::isMessageSeen(uint32_t messageId) {
  for (int i = 0; i < MESH_MSG_CACHE_SIZE; i++) {
    if (_seenMessages[i] == messageId) {
      return true;
    }
  }
  return false;
}

bool MeshManager::verifyCompatPacket(const MeshPacket* packet) {
  if (!packet || packet->header.version != COMPAT_MESH_PROTOCOL_VERSION) {
    return false;
  }

  uint8_t receivedHmac[COMPAT_HMAC_SIZE];
  memcpy(receivedHmac, &packet->payload[packet->header.payloadLen], COMPAT_HMAC_SIZE);

  MeshPacket verify = *packet;

  uint8_t expectedHmac[32];
  if (!MessageAuth::generateHMAC((const char*)&verify, _passphrase, expectedHmac, sizeof(expectedHmac))) {
    return false;
  }

  return memcmp(receivedHmac, expectedHmac, COMPAT_HMAC_SIZE) == 0;
}

bool MeshManager::signCompatPacket(MeshPacket* packet) {
  if (!packet || packet->header.payloadLen > MESH_MAX_PAYLOAD - COMPAT_HMAC_SIZE) {
    return false;
  }

  uint8_t hmac[32];
  if (!MessageAuth::generateHMAC((const char*)packet, _passphrase, hmac, sizeof(hmac))) {
    return false;
  }

  memcpy(&packet->payload[packet->header.payloadLen], hmac, COMPAT_HMAC_SIZE);
  return true;
}

void MeshManager::markMessageSeen(uint32_t messageId) {
  _seenMessages[_seenIndex] = messageId;
  _seenIndex = (_seenIndex + 1) % MESH_MSG_CACHE_SIZE;
}

void MeshManager::updatePeer(const uint8_t* mac, int8_t rssi, const char* unitName, uint8_t hopDistance) {
  if (mac == nullptr) return;

  // Don't track ourselves
  if (memcmp(mac, _myMac, 6) == 0) return;

  // Don't track broadcast address
  if (memcmp(mac, BROADCAST_MAC, 6) == 0) return;

  uint64_t macKey = macToKey(mac);
  auto it = _peerInfo.find(macKey);
  bool isNew = (it == _peerInfo.end());

  if (isNew) {
    // Add new peer
    if (_peerInfo.size() >= MESH_MAX_PEERS) {
      uint64_t evictKey = 0;
      bool evictFound = false;

      for (const auto& entry : _peerInfo) {
        if (!entry.second.isOnline) {
          evictKey = entry.first;
          evictFound = true;
          break;
        }
      }

      if (!evictFound && !_peerInfo.empty()) {
        evictKey = _peerInfo.begin()->first;
        evictFound = true;
      }

      if (evictFound) {
        _peerInfo.erase(evictKey);
        auto peerIt = _peers.find(evictKey);
        if (peerIt != _peers.end()) {
          delete peerIt->second;
          _peers.erase(peerIt);
        }
      }
    }

    MeshPeerInfo newPeer;
    memset(&newPeer, 0, sizeof(newPeer));
    memcpy(newPeer.mac, mac, 6);
    newPeer.rssi = rssi;
    newPeer.hopDistance = hopDistance;
    newPeer.lastSeen = millis();
    newPeer.isOnline = true;
    if (unitName && strlen(unitName) > 0) {
      strncpy(newPeer.unitName, unitName, sizeof(newPeer.unitName) - 1);
      newPeer.unitName[sizeof(newPeer.unitName) - 1] = '\0';
    }
    _peerInfo[macKey] = newPeer;
    it = _peerInfo.find(macKey);
  }

  // Update peer info
  MeshPeerInfo& peer = it->second;
  if (rssi != -127) {
    peer.rssi = rssi;
  }
  peer.lastSeen = millis();
  peer.hopDistance = hopDistance;
  peer.isOnline = true;

  if (unitName && strlen(unitName) > 0) {
    strncpy(peer.unitName, unitName, sizeof(peer.unitName) - 1);
    peer.unitName[sizeof(peer.unitName) - 1] = '\0';
  }

  // Ensure ESP-NOW peer is registered (with LMK for unicast)
  findOrCreatePeer(mac);

  // Notify callback
  if (_peerCallback) {
    _peerCallback(&peer, isNew);
  }
}

void MeshManager::pruneOfflinePeers() {
  uint32_t now = millis();

  for (auto& entry : _peerInfo) {
    MeshPeerInfo& peer = entry.second;
    if (peer.isOnline && (now - peer.lastSeen > MESH_PEER_TIMEOUT_MS)) {
      peer.isOnline = false;

      if (_peerCallback) {
        _peerCallback(&peer, false);
      }
    }
  }
}

MeshPeerInfo* MeshManager::findPeer(const uint8_t* mac) {
  if (!mac) return nullptr;

  uint64_t macKey = macToKey(mac);
  auto it = _peerInfo.find(macKey);
  if (it == _peerInfo.end()) return nullptr;
  return &it->second;
}

void MeshManager::processStoreQueue() {
  uint32_t now = millis();

  for (int i = 0; i < MESH_STORE_QUEUE_SIZE; i++) {
    if (!_storeQueue[i].active) continue;

    // Check if message expired
    if (now - _storeQueue[i].storedTime > MESH_MSG_EXPIRE_MS) {
      _storeQueue[i].active = false;
      output.printf("MeshManager: Stored message %08lX expired\n",
                    (unsigned long)_storeQueue[i].packet.header.messageId);
      continue;
    }

    // Check if target is now online
    MeshPeerInfo* peer = findPeer(_storeQueue[i].targetMac);
    if (peer && peer->isOnline) {
      // Attempt delivery
      deliverStoredMessages(_storeQueue[i].targetMac);
    }
  }
}

void MeshManager::deliverStoredMessages(const uint8_t* peerMac) {
  for (int i = 0; i < MESH_STORE_QUEUE_SIZE; i++) {
    if (!_storeQueue[i].active) continue;

    if (memcmp(_storeQueue[i].targetMac, peerMac, 6) == 0) {
      MeshPacket* packet = &_storeQueue[i].packet;

      // Stored packets are already signed/encrypted - just update mutable header and send
      packet->header.timestamp = millis();
      memcpy(packet->header.lastHopMac, _myMac, 6);

      if (_broadcastPeer &&
          _broadcastPeer->sendMessage((uint8_t*)packet, packetWireSize(packet))) {
        output.printf("MeshManager: Delivered stored message %08lX\n",
                      (unsigned long)packet->header.messageId);
        _msgsSent++;
        _storeQueue[i].retryCount++;
        if (_storeQueue[i].retryCount >= 3) {
          _storeQueue[i].active = false;
        }
      }
    }
  }
}

void MeshManager::sendHeartbeat() {
  // Build heartbeat with unit name
  size_t nameLen = strlen(_unitName);
  broadcast((uint8_t*)_unitName, nameLen, MESH_MSG_HEARTBEAT, 1);  // TTL=1, direct neighbors only
}

uint32_t MeshManager::generateMessageId() {
  return ++_messageIdCounter;
}

bool MeshManager::addEspNowPeer(const uint8_t* mac) {
  return findOrCreatePeer(mac) != nullptr;
}

int MeshManager::getStoredMessageCount() const {
  int count = 0;
  for (int i = 0; i < MESH_STORE_QUEUE_SIZE; i++) {
    if (_storeQueue[i].active) count++;
  }
  return count;
}
