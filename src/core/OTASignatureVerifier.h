#pragma once

#include <Arduino.h>
#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <cstring>

/**
 * OTA Firmware Signature Verification
 * 
 * Validates firmware updates using Ed25519 digital signatures
 * Prevents installation of unsigned or tampered firmware
 * 
 * Workflow:
 * 1. Publisher: Sign firmware with private key → produces signature.bin
 * 2. Device: Load firmware + signature + public key from storage
 * 3. Device: Verify signature(firmware) == signature.bin
 * 4. If valid: Install firmware. If invalid: Reject update.
 * 
 * Uses ECDSA-SHA256 (simpler than Ed25519 on embedded platforms)
 * Public key embedded in firmware (cannot be modified without recompilation)
 */

class OTASignatureVerifier {
public:
    static const uint8_t SIGNATURE_SIZE = 64;  // ECDSA-SHA256 = 64 bytes
    static const uint8_t HASH_SIZE = 32;       // SHA256 = 256 bits = 32 bytes
    static const uint8_t PUBLIC_KEY_SIZE = 64; // ECDSA P-256 public key = 64 bytes

    /**
     * Structure to hold firmware update info
     */
    struct FirmwareUpdate {
        uint8_t* firmwareData = nullptr;
        size_t firmwareSize = 0;
        uint8_t signature[SIGNATURE_SIZE];
        uint32_t firmwareVersion = 0;
        char sourceUrl[256];
    };

    /**
     * Result of signature verification
     */
    enum class VerificationResult {
        VALID = 0,
        INVALID_SIGNATURE = 1,
        HASH_MISMATCH = 2,
        INVALID_FORMAT = 3,
        PUBLIC_KEY_MISMATCH = 4,
        OUT_OF_MEMORY = 5,
        CRYPTO_ERROR = 6
    };

    /**
     * Verify firmware signature using embedded public key
     * 
     * Args:
     *   firmwareData: Pointer to firmware binary
     *   firmwareSize: Length of firmware binary
     *   signature: 64-byte ECDSA signature
     *   publicKey: 64-byte ECDSA P-256 public key (device-specific, embedded)
     * 
     * Returns: VerificationResult enum
     */
    static VerificationResult verify(const uint8_t* firmwareData,
                                    size_t firmwareSize,
                                    const uint8_t* signature,
                                    const uint8_t* publicKey) {
        if (!firmwareData || !signature || !publicKey) {
            return VerificationResult::INVALID_FORMAT;
        }

        // Compute SHA256 hash of firmware
        uint8_t hash[HASH_SIZE];
        if (!_computeSHA256(firmwareData, firmwareSize, hash)) {
            return VerificationResult::CRYPTO_ERROR;
        }

        // Verify ECDSA signature (P-256 curve)
        // Note: Full ECDSA implementation requires mbedtls
        // This is a placeholder - actual implementation depends on available cryptography library
        
        #if defined(USE_MBEDTLS_ECDSA)
            return _verifyECDSA(hash, HASH_SIZE, signature, publicKey);
        #else
            // Fallback: Simple SHA256 comparison (less secure but works)
            // In production: use mbedtls_pk_verify with EC256 key
            return VerificationResult::CRYPTO_ERROR;
        #endif
    }

    /**
     * Extract and store public key from embedded array
     * Device manufacturer embeds this in firmware during compilation
     * 
     * Example in user code:
     *   const uint8_t DEVICE_PUBLIC_KEY[64] PROGMEM = { ... };
     *   OTASignatureVerifier::setPublicKey(DEVICE_PUBLIC_KEY);
     */
    static void setPublicKey(const uint8_t* key) {
        if (key && strlen((const char*)key) > 0) {
            memcpy(_devicePublicKey, key, PUBLIC_KEY_SIZE);
            _publicKeySet = true;
        }
    }

    /**
     * Check if public key has been configured
     */
    static bool isPublicKeySet() {
        return _publicKeySet;
    }

    /**
     * Get human-readable error message
     */
    static const char* getErrorMessage(VerificationResult result) {
        switch (result) {
            case VerificationResult::VALID:
                return "Signature valid - firmware verified";
            case VerificationResult::INVALID_SIGNATURE:
                return "Invalid signature - firmware unsigned or tampered";
            case VerificationResult::HASH_MISMATCH:
                return "Hash mismatch - firmware corrupted";
            case VerificationResult::INVALID_FORMAT:
                return "Invalid format - missing firmware or signature data";
            case VerificationResult::PUBLIC_KEY_MISMATCH:
                return "Public key mismatch - wrong device key";
            case VerificationResult::OUT_OF_MEMORY:
                return "Out of memory - cannot verify firmware";
            case VerificationResult::CRYPTO_ERROR:
                return "Cryptography error - cannot compute hash or verify signature";
            default:
                return "Unknown error";
        }
    }

    /**
     * Safely compare two signatures in constant time
     * Prevents timing attacks
     */
    static bool constantTimeSignatureCompare(const uint8_t* sig1,
                                            const uint8_t* sig2,
                                            size_t sigLen) {
        if (!sig1 || !sig2 || sigLen == 0) {
            return false;
        }

        bool match = true;
        for (size_t i = 0; i < sigLen; i++) {
            if (sig1[i] != sig2[i]) {
                match = false;  // Continue comparing all bytes (timing-safe)
            }
        }
        return match;
    }

    /**
     * Format public key for transmission or storage
     * Converts binary key to hex string
     */
    static String formatPublicKeyAsHex(const uint8_t* key, size_t keyLen) {
        char hexStr[keyLen * 2 + 1];
        for (size_t i = 0; i < keyLen; i++) {
            snprintf(&hexStr[i * 2], 3, "%02x", key[i]);
        }
        hexStr[keyLen * 2] = '\0';
        return String(hexStr);
    }

    /**
     * Parse public key from hex string
     */
    static bool parsePublicKeyFromHex(const String& hexKey, uint8_t* binary, size_t binaryLen) {
        if (hexKey.length() < binaryLen * 2) {
            return false;
        }

        for (size_t i = 0; i < binaryLen; i++) {
            char hex[3] = {hexKey[i * 2], hexKey[i * 2 + 1], '\0'};
            binary[i] = strtol(hex, nullptr, 16);
        }
        return true;
    }

private:
    static uint8_t _devicePublicKey[PUBLIC_KEY_SIZE];
    static bool _publicKeySet;

    /**
     * Compute SHA256 hash of data
     */
    static bool _computeSHA256(const uint8_t* data, size_t dataLen, uint8_t* hash) {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);

        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (!md_info) {
            mbedtls_md_free(&ctx);
            return false;
        }

        if (mbedtls_md_setup(&ctx, md_info, 0) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }

        if (mbedtls_md_starts(&ctx) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }

        if (mbedtls_md_update(&ctx, data, dataLen) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }

        if (mbedtls_md_finish(&ctx, hash) != 0) {
            mbedtls_md_free(&ctx);
            return false;
        }

        mbedtls_md_free(&ctx);
        return true;
    }

    /**
     * Verify ECDSA signature (requires mbedtls and ec256 support)
     * Implementation depends on available crypto libraries
     */
    static VerificationResult _verifyECDSA(const uint8_t* hash,
                                          size_t hashLen,
                                          const uint8_t* signature,
                                          const uint8_t* publicKey) {
        // Placeholder for actual ECDSA verification
        // In production, would use:
        // - mbedtls_pk_parse_public_key()
        // - mbedtls_pk_verify() with ECDSA
        // - Return appropriate VerificationResult
        
        return VerificationResult::CRYPTO_ERROR;  // Not yet implemented
    }
};

// Static member initialization
uint8_t OTASignatureVerifier::_devicePublicKey[OTASignatureVerifier::PUBLIC_KEY_SIZE] = {0};
bool OTASignatureVerifier::_publicKeySet = false;

/**
 * OTA Update Manager
 * 
 * Handles firmware update workflow with signature verification
 * 
 * Usage:
 *   OTAUpdateManager ota;
 *   ota.setPublicKey(devicePublicKey);
 *   bool success = ota.verifyAndInstall("https://firmware-server/fw-v2.bin", signature);
 */

class OTAUpdateManager {
public:
    enum class UpdateState {
        IDLE,
        DOWNLOADING,
        VERIFYING,
        INSTALLING,
        COMPLETE,
        FAILED
    };

    OTAUpdateManager() : _state(UpdateState::IDLE) {}

    /**
     * Set the public key for this device
     */
    void setPublicKey(const uint8_t* key, size_t keyLen) {
        if (keyLen == OTASignatureVerifier::PUBLIC_KEY_SIZE) {
            memcpy(_publicKey, key, keyLen);
            _publicKeySet = true;
        }
    }

    /**
     * Verify firmware before installation
     */
    bool verifyFirmware(const uint8_t* firmwareData, size_t firmwareSize,
                       const uint8_t* signature) {
        if (!_publicKeySet) {
            _lastError = "Public key not configured";
            _state = UpdateState::FAILED;
            return false;
        }

        _state = UpdateState::VERIFYING;

        OTASignatureVerifier::VerificationResult result =
            OTASignatureVerifier::verify(firmwareData, firmwareSize,
                                        signature, _publicKey);

        if (result == OTASignatureVerifier::VerificationResult::VALID) {
            _state = UpdateState::VERIFYING;
            return true;
        } else {
            _lastError = OTASignatureVerifier::getErrorMessage(result);
            _state = UpdateState::FAILED;
            return false;
        }
    }

    /**
     * Get current update state
     */
    UpdateState getState() const {
        return _state;
    }

    /**
     * Get last error message
     */
    const char* getLastError() const {
        return _lastError.c_str();
    }

    /**
     * Set firmware version for compatibility checking
     */
    void setCurrentVersion(uint32_t version) {
        _currentVersion = version;
    }

    /**
     * Check if firmware version is newer than current
     */
    bool isNewerVersion(uint32_t newVersion) {
        return newVersion > _currentVersion;
    }

private:
    UpdateState _state;
    uint8_t _publicKey[OTASignatureVerifier::PUBLIC_KEY_SIZE];
    bool _publicKeySet = false;
    uint32_t _currentVersion = 0;
    String _lastError;
};
