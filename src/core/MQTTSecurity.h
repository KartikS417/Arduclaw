#pragma once

#include <Arduino.h>
#include <mbedtls/md.h>
#include <cstring>

/**
 * MQTT Security Helper
 * Provides HMAC-SHA256 signing/verification for MQTT messages
 */

class MQTTSecurity {
public:
    static const uint8_t HMAC_SIZE = 32;  // SHA256 = 256 bits = 32 bytes
    static const uint8_t HMAC_HEX_SIZE = 64;  // 32 bytes * 2 hex chars = 64 chars

    /**
     * Convert hex string (32 bytes) to binary key (16 bytes)
     * Assumes key is 64-char hex string representing 32 bytes
     */
    static bool hexToBinary(const String& hexKey, uint8_t* binaryKey, size_t binarySize) {
        if (hexKey.length() < HMAC_HEX_SIZE || binarySize < 32) {
            return false;
        }

        for (int i = 0; i < 32; i++) {
            char hex[3] = {hexKey[i * 2], hexKey[i * 2 + 1], '\0'};
            binaryKey[i] = strtol(hex, nullptr, 16);
        }
        return true;
    }

    /**
     * Sign a message with HMAC-SHA256
     * Returns hex-encoded signature (64 characters)
     */
    static String signMessage(const uint8_t* key, size_t keyLen, 
                             const char* message, size_t messageLen) {
        uint8_t hmac[HMAC_SIZE];
        
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (!md_info) {
            mbedtls_md_free(&ctx);
            return "";
        }

        if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        if (mbedtls_md_hmac_starts(&ctx, key, keyLen) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        if (mbedtls_md_hmac_update(&ctx, (const uint8_t*)message, messageLen) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        if (mbedtls_md_hmac_finish(&ctx, hmac) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        mbedtls_md_free(&ctx);

        // Convert to hex string
        char hexSignature[HMAC_HEX_SIZE + 1];
        for (int i = 0; i < HMAC_SIZE; i++) {
            snprintf(&hexSignature[i * 2], 3, "%02x", hmac[i]);
        }
        hexSignature[HMAC_HEX_SIZE] = '\0';

        return String(hexSignature);
    }

    /**
     * Verify a message signature
     */
    static bool verifyMessage(const uint8_t* key, size_t keyLen,
                             const char* message, size_t messageLen,
                             const String& providedSignature) {
        String computedSignature = signMessage(key, keyLen, message, messageLen);
        if (computedSignature.length() != providedSignature.length()) {
            return false;
        }
        return computedSignature == providedSignature;
    }

    /**
     * Convenience function using hex key directly
     */
    static String signWithHexKey(const String& hexKey, const char* message, size_t messageLen) {
        uint8_t binaryKey[32];
        if (!hexToBinary(hexKey, binaryKey, sizeof(binaryKey))) {
            return "";
        }
        return signMessage(binaryKey, 32, message, messageLen);
    }

    /**
     * Convenience function for verification using hex key
     */
    static bool verifyWithHexKey(const String& hexKey, const char* message, 
                                size_t messageLen, const String& signature) {
        uint8_t binaryKey[32];
        if (!hexToBinary(hexKey, binaryKey, sizeof(binaryKey))) {
            return false;
        }
        return verifyMessage(binaryKey, 32, message, messageLen, signature);
    }
};
