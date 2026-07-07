#include "MessageAuth.h"
#include <cstring>

bool MessageAuth::generateHMAC(const char* message, const char* key, uint8_t* hmac, size_t hmacLen) {
  if (!message || !key || !hmac || hmacLen < 32) {
    return false;
  }

  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);

  // Setup HMAC-SHA256
  if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  // Set key
  if (mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key, strlen(key)) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  // Hash message
  if (mbedtls_md_hmac_update(&ctx, (const unsigned char*)message, strlen(message)) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  // Finalize HMAC
  if (mbedtls_md_hmac_finish(&ctx, hmac) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  mbedtls_md_free(&ctx);
  return true;
}

bool MessageAuth::verifyHMAC(const char* message, const char* key, const uint8_t* receivedHmac, size_t hmacLen) {
  if (!message || !key || !receivedHmac || hmacLen == 0) {
    return false;
  }

  uint8_t computedHmac[32];
  if (!generateHMAC(message, key, computedHmac, 32)) {
    return false;
  }

  // Compare only the first hmacLen bytes
  size_t compareLen = (hmacLen < 32) ? hmacLen : 32;
  return memcmp(computedHmac, receivedHmac, compareLen) == 0;
}

void MessageAuth::sanitizeInput(char* input, size_t maxLen) {
  if (!input) return;

  for (size_t i = 0; i < maxLen && input[i] != '\0'; i++) {
    // Replace control characters (0x00-0x1F) with space
    if (input[i] < 0x20 && input[i] != '\0') {
      input[i] = ' ';
    }
    // Replace DEL character (0x7F) with space
    if (input[i] == 0x7F) {
      input[i] = ' ';
    }
  }

  // Ensure null termination
  input[maxLen - 1] = '\0';
}

void MessageAuth::toHex(const uint8_t* hmac, size_t hmacLen, char* output, size_t outputSize) {
  if (!hmac || !output || outputSize < (hmacLen * 2 + 1)) return;

  for (size_t i = 0; i < hmacLen; i++) {
    sprintf(&output[i * 2], "%02x", hmac[i]);
  }
  output[hmacLen * 2] = '\0';
}

void MessageAuth::hmacToHex(const uint8_t* hmac, size_t hmacLen, char* output) {
  if (!hmac || !output) return;

  for (size_t i = 0; i < hmacLen; i++) {
    sprintf(&output[i * 2], "%02x", hmac[i]);
  }
  output[hmacLen * 2] = '\0';
}

bool MessageAuth::hexToHmac(const char* hexStr, uint8_t* hmac, size_t hmacLen) {
  if (!hexStr || !hmac) return false;

  size_t hexLen = strlen(hexStr);
  if (hexLen != hmacLen * 2) {
    return false;
  }

  for (size_t i = 0; i < hmacLen; i++) {
    int byte;
    if (sscanf(&hexStr[i * 2], "%2x", &byte) != 1) {
      return false;
    }
    hmac[i] = (uint8_t)byte;
  }

  return true;
}
