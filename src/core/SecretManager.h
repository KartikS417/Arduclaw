#pragma once

#include <Arduino.h>
#include <cstring>

#if defined(ARDUINO_ARCH_ESP32)
    #include <esp_mac.h>
#endif

/**
 * SecretManager - Simple XOR Encryption
 * 
 * Encrypts sensitive strings (API keys) at rest using XOR cipher
 * Key = Device MAC address (6 bytes) + hardcoded salt (4 bytes)
 * 
 * NOT cryptographically secure vs determined attacker, but:
 * - Prevents casual plaintext dumps
 * - Minimal overhead (~20 bytes per secret)
 * - Works on all MCUs (no crypto libraries needed)
 * 
 * For true security: implement AES-128-CBC with mbedtls
 */

class SecretManager {
private:
    // Hardcoded salt - change this per deployment
    static constexpr const char* SALT = "ACLW";
    static constexpr uint8_t SALT_LEN = 4;

    /**
     * Get device MAC address for unique per-device key
     * Returns 6-byte MAC or all zeros if unavailable
     */
    static void getDeviceMac(uint8_t* mac) {
        // Try to get WiFi MAC (requires WiFi to be initialized)
        #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
        #else
            memset(mac, 0, 6);
        #endif
    }

    /**
     * Generate XOR key from MAC + salt
     */
    static void generateKey(uint8_t* key, size_t keyLen) {
        uint8_t mac[6];
        getDeviceMac(mac);
        
        // Combine MAC + salt into key
        for (size_t i = 0; i < keyLen; i++) {
            if (i < 6) {
                key[i] = mac[i];
            } else if (i < 6 + SALT_LEN) {
                key[i] = SALT[i - 6];
            } else {
                // Cycle through if key is longer
                key[i] = mac[i % 6] ^ SALT[(i - 6) % SALT_LEN];
            }
        }
    }

public:
    /**
     * Encrypt a plaintext secret string
     * Returns encrypted bytes as hex string
     * 
     * Example:
     *   String encrypted = SecretManager::encrypt("my-api-key");
     *   // Store encrypted in SPIFFS/config
     */
    static String encrypt(const String& plaintext) {
        if (plaintext.length() == 0) {
            return "";
        }

        size_t textLen = plaintext.length();
        uint8_t key[16];
        generateKey(key, sizeof(key));
        
        // Allocate buffer for encrypted data
        uint8_t* encrypted = new uint8_t[textLen];
        if (!encrypted) {
            return "";  // Memory allocation failed
        }

        // XOR encryption
        for (size_t i = 0; i < textLen; i++) {
            encrypted[i] = plaintext[i] ^ key[i % sizeof(key)];
        }

        // Convert to hex string
        char* hexStr = new char[textLen * 2 + 1];
        if (!hexStr) {
            delete[] encrypted;
            return "";
        }

        for (size_t i = 0; i < textLen; i++) {
            snprintf(&hexStr[i * 2], 3, "%02x", encrypted[i]);
        }
        hexStr[textLen * 2] = '\0';

        String result(hexStr);
        delete[] hexStr;
        delete[] encrypted;
        return result;
    }

    /**
     * Decrypt a previously encrypted secret
     * Input is hex-encoded encrypted data
     * 
     * Example:
     *   String plaintext = SecretManager::decrypt(encryptedHex);
     */
    static String decrypt(const String& encryptedHex) {
        if (encryptedHex.length() == 0 || encryptedHex.length() % 2 != 0) {
            return "";
        }

        size_t encLen = encryptedHex.length() / 2;
        uint8_t key[16];
        generateKey(key, sizeof(key));

        // Allocate buffer for decrypted data
        uint8_t* encrypted = new uint8_t[encLen];
        if (!encrypted) {
            return "";
        }

        // Convert from hex string to bytes
        for (size_t i = 0; i < encLen; i++) {
            char hex[3] = {encryptedHex[i * 2], encryptedHex[i * 2 + 1], '\0'};
            encrypted[i] = strtol(hex, nullptr, 16);
        }

        // XOR decryption (same operation as encryption)
        char* plaintext = new char[encLen + 1];
        if (!plaintext) {
            delete[] encrypted;
            return "";
        }

        for (size_t i = 0; i < encLen; i++) {
            plaintext[i] = encrypted[i] ^ key[i % sizeof(key)];
        }
        plaintext[encLen] = '\0';

        String result(plaintext);
        delete[] plaintext;
        delete[] encrypted;
        return result;
    }

    /**
     * Check if a secret appears to be encrypted (is valid hex string)
     */
    static bool isEncrypted(const String& secret) {
        if (secret.length() == 0 || secret.length() % 2 != 0) {
            return false;
        }

        // Check if all characters are hex digits
        for (int i = 0; i < secret.length(); i++) {
            char c = secret[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        return true;
    }
};
