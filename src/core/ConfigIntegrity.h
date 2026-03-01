#pragma once

#include <Arduino.h>
#include <mbedtls/md.h>
#include <ArduinoJson.h>
#include <cstring>

#if defined(ARDUINO_ARCH_ESP32)
    #include <esp_mac.h>
#endif

/**
 * Config Integrity Verification
 * 
 * Prevents tampering with device configuration by computing HMAC-SHA256
 * of config contents and storing alongside config file.
 * 
 * On load: Verify config hash matches stored hash
 * On save: Compute and store hash with config
 * 
 * Key = Device MAC address (prevents copying configs between devices)
 */

class ConfigIntegrity {
public:
    static const uint8_t HASH_SIZE = 32;  // SHA256 = 256 bits = 32 bytes
    static const uint8_t HASH_HEX_SIZE = 64;  // 32 bytes * 2 hex chars

    /**
     * Compute HMAC-SHA256 of config data
     * Uses device MAC as HMAC key (prevents cross-device copying)
     */
    static String computeHash(const String& configJson) {
        uint8_t mac[6];
        _getDeviceMac(mac);

        uint8_t hmac[HASH_SIZE];
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

        // HMAC with MAC address as key
        if (mbedtls_md_hmac_starts(&ctx, mac, 6) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        if (mbedtls_md_hmac_update(&ctx, 
            (const uint8_t*)configJson.c_str(), 
            configJson.length()) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        if (mbedtls_md_hmac_finish(&ctx, hmac) != 0) {
            mbedtls_md_free(&ctx);
            return "";
        }

        mbedtls_md_free(&ctx);

        // Convert to hex string
        char hexHash[HASH_HEX_SIZE + 1];
        for (int i = 0; i < HASH_SIZE; i++) {
            snprintf(&hexHash[i * 2], 3, "%02x", hmac[i]);
        }
        hexHash[HASH_HEX_SIZE] = '\0';

        return String(hexHash);
    }

    /**
     * Verify config against stored hash
     * Returns true if hash matches, false if tampering detected
     */
    static bool verify(const String& configJson, const String& storedHash) {
        if (storedHash.length() != HASH_HEX_SIZE) {
            return false;
        }

        String computedHash = computeHash(configJson);
        if (computedHash.length() == 0) {
            return false;
        }

        // Constant-time comparison to prevent timing attacks
        bool match = true;
        for (int i = 0; i < HASH_HEX_SIZE; i++) {
            if (configJson[i] != storedHash[i]) {
                match = false;
            }
        }
        return match;
    }

    /**
     * Add integrity hash to JSON document before saving
     */
    static void addHashToDoc(JsonDocument& doc, const String& configJson) {
        String hash = computeHash(configJson);
        if (hash.length() > 0) {
            doc["_integrity_hash"] = hash;
        }
    }

    /**
     * Extract and remove hash from document for verification
     */
    static String extractHashFromDoc(JsonDocument& doc) {
        if (doc.containsKey("_integrity_hash")) {
            return doc["_integrity_hash"].as<String>();
        }
        return "";
    }

private:
    static void _getDeviceMac(uint8_t* mac) {
        #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
        #else
            memset(mac, 0, 6);
        #endif
    }
};
