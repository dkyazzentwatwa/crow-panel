#pragma once

#include <Arduino.h>
#include <mbedtls/md.h>

/**
 * MessageAuth - HMAC-SHA256 authentication for BLE messages
 *
 * Prevents message spoofing and tampering by adding HMAC signatures
 * derived from the shared passkey.
 */
class MessageAuth {
public:
  /**
   * Generate HMAC-SHA256 signature for a message
   * @param message Base message to sign
   * @param key Passkey used as HMAC key
   * @param hmac Output buffer for HMAC (must be at least 32 bytes)
   * @param hmacLen Size of output buffer
   * @return true if HMAC generated successfully
   */
  static bool generateHMAC(const char* message, const char* key, uint8_t* hmac, size_t hmacLen);

  /**
   * Verify HMAC signature for a message
   * @param message Base message to verify
   * @param key Passkey used as HMAC key
   * @param receivedHmac HMAC to verify against
   * @param hmacLen Length of received HMAC
   * @return true if HMAC matches
   */
  static bool verifyHMAC(const char* message, const char* key, const uint8_t* receivedHmac, size_t hmacLen);

  /**
   * Sanitize input string to prevent control characters
   * @param input String to sanitize (modified in place)
   * @param maxLen Maximum length of input buffer
   */
  static void sanitizeInput(char* input, size_t maxLen);

  /**
   * Convert HMAC bytes to hex string
   * @param hmac HMAC bytes
   * @param hmacLen Length of HMAC bytes
   * @param output Output buffer for hex string (must be at least hmacLen*2 + 1)
   * @param outputSize Size of output buffer
   */
  static void toHex(const uint8_t* hmac, size_t hmacLen, char* output, size_t outputSize);

  /**
   * Convert HMAC bytes to hex string (legacy alias)
   * @param hmac HMAC bytes
   * @param hmacLen Length of HMAC bytes
   * @param output Output buffer for hex string (must be at least hmacLen*2 + 1)
   */
  static void hmacToHex(const uint8_t* hmac, size_t hmacLen, char* output);

  /**
   * Convert hex string to HMAC bytes
   * @param hexStr Hex string to convert
   * @param hmac Output buffer for HMAC bytes
   * @param hmacLen Expected length of HMAC bytes
   * @return true if conversion successful
   */
  static bool hexToHmac(const char* hexStr, uint8_t* hmac, size_t hmacLen);
};
