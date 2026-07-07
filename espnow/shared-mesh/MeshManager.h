#pragma once

#include <Arduino.h>
#include "ESP32_NOW.h"
#include <esp_mac.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include "MeshCrypto.h"

/**
 * MeshManager - ESP-NOW based mesh networking for Cypher-Chat
 *
 * Features:
 * - Multi-hop message relay with TTL
 * - Store-and-forward for offline nodes
 * - Message deduplication (seen cache)
 * - Automatic peer discovery
 * - Works alongside BLE (hybrid mode)
 *
 * Range: ~250m line-of-sight (vs BLE's ~30m)
 * Latency: ~1ms per hop
 */

// Mesh protocol constants
#define MESH_MAX_PEERS        20      // Maximum tracked peers
#define MESH_MAX_TTL          5       // Maximum hops before discard
#define MESH_DEFAULT_TTL      3       // Default TTL for new messages
#define MESH_MSG_CACHE_SIZE   64      // Seen message cache (deduplication)
#define MESH_STORE_QUEUE_SIZE 32      // Store-and-forward queue size
#define MESH_MSG_EXPIRE_MS    300000  // Messages expire after 5 minutes
#define MESH_PEER_TIMEOUT_MS  60000   // Peer considered offline after 60s
#define MESH_HEARTBEAT_MS     15000   // Heartbeat interval
#define MESH_CHANNEL          1       // WiFi channel for ESP-NOW
#define MESH_EMERGENCY_COOLDOWN_MS 10000  // Rate limit: 1 emergency per 10s per sender
#define MESH_MAX_PLAINTEXT    (MESH_MAX_PAYLOAD - MESH_CRYPTO_OVERHEAD)  // Max plaintext after crypto overhead

// Message types for mesh protocol
enum MeshMessageType : uint8_t {
  MESH_MSG_DATA      = 0x01,  // Regular data message
  MESH_MSG_EMERGENCY = 0x02,  // Emergency broadcast (high priority)
  MESH_MSG_ACK       = 0x03,  // Delivery acknowledgment
  MESH_MSG_HEARTBEAT = 0x04,  // Peer discovery/keepalive
  MESH_MSG_FORWARD   = 0x05,  // Store-and-forward delivery
  MESH_MSG_GPS       = 0x06   // GPS location broadcast
};

// Mesh message header (fixed 32 bytes for efficiency)
struct __attribute__((packed)) MeshHeader {
  uint8_t  version;           // Protocol version (0x01)
  uint8_t  type;              // MeshMessageType
  uint8_t  ttl;               // Time-to-live (hops remaining)
  uint8_t  hopCount;          // Number of hops taken
  uint32_t messageId;         // Unique message identifier
  uint8_t  originMac[6];      // Original sender MAC
  uint8_t  destMac[6];        // Destination MAC (broadcast: FF:FF:FF:FF:FF:FF)
  uint8_t  lastHopMac[6];     // Previous relay node MAC
  uint32_t timestamp;         // Message creation time (relative ms)
  uint8_t  payloadLen;        // Length of payload
  uint8_t  flags;             // Flags: 0x01=needsAck, 0x02=isRelay, 0x04=hasGPS
};

// Full mesh packet (header + payload)
#define MESH_MAX_PAYLOAD 200  // ESP-NOW limit is 250 bytes
struct __attribute__((packed)) MeshPacket {
  MeshHeader header;
  uint8_t payload[MESH_MAX_PAYLOAD];
};

// Peer info for tracking neighbors
struct MeshPeerInfo {
  uint8_t mac[6];
  int8_t rssi;                // Signal strength
  uint32_t lastSeen;          // Last heartbeat time
  uint8_t hopDistance;        // Hops to this peer (1 = direct)
  bool isOnline;
  char unitName[17];          // Peer's unit name
};

// Store-and-forward entry
struct StoreForwardEntry {
  MeshPacket packet;
  uint8_t targetMac[6];
  uint32_t storedTime;
  uint8_t retryCount;
  bool active;
};

// GPS coordinates for emergency broadcasts
struct GPSCoordinates {
  float latitude;
  float longitude;
  float altitude;
  bool valid;
};

// Callback types for mesh events
typedef void (*MeshMessageCallback)(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi);
typedef void (*MeshPeerCallback)(const MeshPeerInfo* peer, bool isNew);

/**
 * MeshPeer - Custom ESP_NOW_Peer for mesh networking
 *
 * Handles mesh-specific message processing and callbacks.
 * Each peer in the mesh network is represented by this class.
 */
class MeshPeer : public ESP_NOW_Peer {
public:
  // Constructor for mesh peers
  MeshPeer(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk)
    : ESP_NOW_Peer(mac_addr, channel, iface, lmk)
    , _manager(nullptr)
    , _isRegistered(false) {
  }

  // Destructor
  virtual ~MeshPeer() {
    if (_isRegistered) {  // Only remove if successfully registered
      remove();
    }
  }

  // Initialize and register peer
  bool begin() {
    _isRegistered = add();  // Track registration state
    return _isRegistered;
  }

  // Send message to this peer
  bool sendMessage(const uint8_t *data, size_t len) {
    return send(data, len) > 0;
  }

  // Set manager reference for callbacks
  void setManager(class MeshManager *manager) {
    _manager = manager;
  }

protected:
  // Override: Called when data received from this peer
  void onReceive(const uint8_t *data, size_t len, bool broadcast) override;

  // Override: Called when send to this peer completes
  void onSent(bool success) override;

private:
  class MeshManager *_manager;  // Reference to manager for callbacks
  bool _isRegistered;            // Track if peer was successfully registered
};

class MeshManager {
  friend class MeshPeer;
  friend void onNewPeerPacket(const esp_now_recv_info_t*, const uint8_t*, int, void*);
public:
  MeshManager();

  /**
   * Initialize ESP-NOW mesh networking
   * @param unitName This device's identifier
   * @param passphrase Shared passphrase for AEAD encryption
   * @return true if initialization successful
   */
  bool begin(const char* unitName, const char* passphrase);

  /**
   * Stop mesh networking and release resources
   */
  void end();

  /**
   * Process mesh events - call from loop()
   */
  void update();

  /**
   * Send a message to all mesh peers (broadcast flood)
   * @param data Message payload
   * @param len Payload length
   * @param type Message type
   * @param ttl Time-to-live (default: MESH_DEFAULT_TTL)
   * @return Message ID if sent, 0 on failure
   */
  uint32_t broadcast(const uint8_t* data, size_t len, MeshMessageType type = MESH_MSG_DATA, uint8_t ttl = MESH_DEFAULT_TTL);

  /**
   * Send a message to a specific peer (unicast with mesh routing)
   * @param destMac Destination MAC address
   * @param data Message payload
   * @param len Payload length
   * @param type Message type
   * @return Message ID if sent, 0 on failure
   */
  uint32_t sendTo(const uint8_t* destMac, const uint8_t* data, size_t len, MeshMessageType type = MESH_MSG_DATA);

  /**
   * Send emergency broadcast with optional GPS
   * @param message Emergency message
   * @param gps Optional GPS coordinates
   * @return Message ID if sent, 0 on failure
   */
  uint32_t sendEmergency(const char* message, const GPSCoordinates* gps = nullptr);

  /**
   * Store a message for later delivery (store-and-forward)
   * @param destMac Destination MAC
   * @param data Message payload
   * @param len Payload length
   * @return true if stored successfully
   */
  bool storeForDelivery(const uint8_t* destMac, const uint8_t* data, size_t len);

  /**
   * Set callback for received messages
   * @param callback Function to call when message received
   */
  void onMessage(MeshMessageCallback callback);

  /**
   * Set callback for peer discovery events
   * @param callback Function to call when peer discovered/updated
   */
  void onPeerUpdate(MeshPeerCallback callback);

  /**
   * Get list of known peers
   * @return Vector of peer info
   */
  std::vector<MeshPeerInfo> getPeers() const;

  /**
   * Get count of online peers
   * @return Number of peers seen recently
   */
  int getOnlinePeerCount() const;

  /**
   * Get this device's MAC address
   * @param mac Output buffer (6 bytes)
   */
  void getMyMac(uint8_t* mac) const;

  /**
   * Check if mesh is running
   * @return true if initialized and running
   */
  bool isRunning() const { return _running; }

  /**
   * Get statistics
   */
  uint32_t getMessagesSent() const { return _msgsSent; }
  uint32_t getMessagesReceived() const { return _msgsReceived; }
  uint32_t getMessagesRelayed() const { return _msgsRelayed; }
  uint32_t getMessagesDropped() const { return _msgsDropped; }

  /**
   * Set GPS coordinates for this device
   * @param gps GPS coordinates
   */
  void setGPS(const GPSCoordinates& gps);

  /**
   * Get current GPS coordinates
   * @return GPS coordinates (check .valid field)
   */
  GPSCoordinates getGPS() const { return _myGPS; }

  /**
   * Get count of stored messages pending delivery
   * @return Number of messages in store-and-forward queue
   */
  int getStoredMessageCount() const;

private:
  // Internal message handling
  void processPacket(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi);
  void processCompatPacket(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi);
  void handleDataMessage(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi);
  void handleEmergency(const MeshPacket* packet, const uint8_t* senderMac);
  void handleAck(const MeshPacket* packet);
  void handleHeartbeat(const MeshPacket* packet, const uint8_t* senderMac, int8_t rssi);

  // Mesh routing
  bool shouldRelay(const MeshPacket* packet);
  void relayPacket(MeshPacket* packet);
  bool isMessageSeen(uint32_t messageId);
  void markMessageSeen(uint32_t messageId);

  // Peer management
  void updatePeer(const uint8_t* mac, int8_t rssi, const char* unitName, uint8_t hopDistance);
  void pruneOfflinePeers();
  MeshPeerInfo* findPeer(const uint8_t* mac);

  // Store-and-forward
  void processStoreQueue();
  void deliverStoredMessages(const uint8_t* peerMac);

  // Heartbeat
  void sendHeartbeat();

  // Utility
  uint32_t generateMessageId();
  bool addEspNowPeer(const uint8_t* mac);
  bool signCompatPacket(MeshPacket* packet);
  bool verifyCompatPacket(const MeshPacket* packet);

  // Peer management for new API
  MeshPeer* findOrCreatePeer(const uint8_t* mac);
  void handlePeerReceive(const uint8_t* senderMac, const uint8_t* data, size_t len);
  void handlePeerSent(const uint8_t* peerMac, bool success);

  // State
  bool _running;
  char _unitName[17];
  char _passphrase[65];
  uint8_t _myMac[6];
  GPSCoordinates _myGPS;

  // Callbacks
  MeshMessageCallback _messageCallback;
  MeshPeerCallback _peerCallback;

  // Peer tracking
  std::map<uint64_t, MeshPeer*> _peers;  // MAC as uint64_t key -> peer object
  std::map<uint64_t, MeshPeerInfo> _peerInfo;  // MAC as uint64_t key -> info
  MeshPeer* _broadcastPeer;  // Special peer for broadcast messages

  // Message deduplication (circular buffer of seen message IDs)
  uint32_t _seenMessages[MESH_MSG_CACHE_SIZE];
  int _seenIndex;

  // Store-and-forward queue
  StoreForwardEntry _storeQueue[MESH_STORE_QUEUE_SIZE];

  // Timing
  uint32_t _lastHeartbeat;
  uint32_t _lastPeerPrune;

  // Statistics
  uint32_t _msgsSent;
  uint32_t _msgsReceived;
  uint32_t _msgsRelayed;
  uint32_t _msgsDropped;
  uint32_t _messageIdCounter;

  // MAC randomization
  uint32_t _lastMacRotation;
  uint8_t _originalMac[6];  // Store original MAC for reference

  // MAC randomization helper
  void randomizeMac();

  // Emergency rate limiting: MAC -> last emergency timestamp
  std::map<uint64_t, uint32_t> _emergencyRateLimit;
};

// Global instance
extern MeshManager meshMgr;
