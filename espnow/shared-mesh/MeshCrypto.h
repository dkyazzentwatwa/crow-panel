#pragma once

#include <Arduino.h>
#include <map>

/**
 * MeshCrypto - Application-layer encryption for ESP-NOW mesh
 *
 * Provides:
 * - HKDF-SHA256 key derivation from passphrase
 * - AES-256-GCM AEAD encryption (confidentiality + authentication)
 * - Per-sender monotonic replay counter protection
 * - LMK key derivation for ESP-NOW link-layer encryption
 * - NVS persistence for replay counters and passphrase
 *
 * Wire format for encrypted payload:
 *   [iv:12][ciphertext:N][gcm_tag:16]
 *
 * AAD (authenticated but not encrypted) = immutable header fields:
 *   [version:1][type:1][messageId:4][originMac:6][destMac:6][payloadLen:1]
 */

// Crypto constants
#define MESH_CRYPTO_KEY_SIZE       32   // AES-256-GCM key (256-bit)
#define MESH_CRYPTO_NONCE_SIZE     12   // AES-GCM IV/nonce
#define MESH_CRYPTO_TAG_SIZE       16   // GCM auth tag
#define MESH_CRYPTO_OVERHEAD       (MESH_CRYPTO_NONCE_SIZE + MESH_CRYPTO_TAG_SIZE)  // 28 bytes
#define MESH_CRYPTO_LMK_SIZE       16   // ESP-NOW LMK key size
#define MESH_CRYPTO_AAD_SIZE       19   // Immutable header fields for AAD
#define MESH_REPLAY_MAX_PEERS      20   // Max tracked senders for replay counters

// Forward declaration
struct MeshPacket;
struct MeshHeader;

class MeshCrypto {
public:
  /**
   * Initialize crypto with a passphrase
   * Derives encryption key and LMK base key via HKDF-SHA256
   * @param passphrase User passphrase (any length, UTF-8)
   * @return true if initialization successful
   */
  static bool init(const char* passphrase);

  /**
   * Encrypt a mesh packet payload in-place
   * Generates random nonce, encrypts payload, appends tag
   * After encryption, payload buffer contains: [nonce:12][ciphertext:N][tag:16]
   * @param packet Packet with plaintext in payload[0..payloadLen-1]
   * @return true if encryption successful
   */
  static bool encryptPayload(MeshPacket* packet);

  /**
   * Decrypt a mesh packet payload in-place
   * Extracts nonce, verifies tag, decrypts ciphertext
   * After decryption, payload buffer contains plaintext[0..payloadLen-1]
   * @param packet Packet with encrypted data in payload buffer
   * @return true if decryption and authentication successful
   */
  static bool decryptPayload(MeshPacket* packet);

  /**
   * Check replay counter for a message
   * Rejects messages with messageId <= last seen from that sender
   * @param originMac Sender MAC address (6 bytes)
   * @param messageId Message ID to check
   * @return true if message is NEW (not a replay)
   */
  static bool checkReplayCounter(const uint8_t* originMac, uint32_t messageId);

  /**
   * Derive LMK for a specific peer
   * LMK = HMAC-SHA256(lmkBaseKey, peerMac)[0:16]
   * @param peerMac Peer MAC address (6 bytes)
   * @param lmkOut Output buffer (16 bytes)
   * @return true if derivation successful
   */
  static bool deriveLMK(const uint8_t* peerMac, uint8_t* lmkOut);

  /**
   * Rotate to a new passphrase
   * Re-derives all keys, saves to NVS
   * @param newPassphrase New passphrase
   * @return true if rotation successful
   */
  static bool rotateKey(const char* newPassphrase);

  /**
   * Save replay counters to NVS for persistence across reboots
   */
  static void saveReplayCounters();

  /**
   * Load replay counters from NVS
   */
  static void loadReplayCounters();

  /**
   * Save passphrase to NVS
   * Stores: encrypted passphrase + verification hash (NOT plaintext)
   * Uses a device-unique key derived from ESP32 MAC for encryption
   * @param passphrase Passphrase to save
   */
  static void savePassphrase(const char* passphrase);

  /**
   * Load passphrase from NVS
   * Decrypts stored passphrase and verifies against stored hash
   * @param buffer Output buffer
   * @param bufSize Buffer size
   * @return true if passphrase was loaded and verified
   */
  static bool loadPassphrase(char* buffer, size_t bufSize);

  /**
   * Clear saved passphrase from NVS
   * Used when user wants to reset passphrase on next boot
   */
  static void clearSavedPassphrase();

  /**
   * Clear all replay counters (call on mesh init to prevent false replay detection)
   */
  static void clearReplayCounters();

  /**
   * Check if crypto is initialized
   */
  static bool isInitialized() { return _initialized; }

private:
  // Key derivation via HKDF-SHA256
  static bool deriveKeys(const char* passphrase);

  // Build AAD from immutable header fields
  static void buildAAD(const MeshHeader* header, uint8_t* aad);

  // Convert MAC to uint64_t key for map lookup
  static uint64_t macToKey(const uint8_t* mac);

  // State
  static bool _initialized;
  static uint8_t _encKey[MESH_CRYPTO_KEY_SIZE];       // AES-256-GCM key
  static uint8_t _lmkBaseKey[MESH_CRYPTO_LMK_SIZE];   // LMK derivation base

  // Replay counter tracking: MAC -> highest seen messageId
  static std::map<uint64_t, uint32_t> _replayCounters;
};
