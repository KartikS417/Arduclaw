#pragma once
#include <Arduino.h>
#include <cstring>

/**
 * Rate Limiting & Brute-Force Protection
 * 
 * Tracks failed attempts and enforces exponential backoff:
 * - 1-2 failures: Allow immediately
 * - 3 failures: 30 second lockout
 * - 4 failures: 2 minute lockout
 * - 5+ failures: 10 minute lockout
 */

class RateLimiter {
private:
    unsigned long lastRequest = 0;
    unsigned long intervalMs;

public:
    RateLimiter(unsigned long interval)
        : intervalMs(interval) {}

    bool allow() {
        if (millis() - lastRequest >= intervalMs) {
            lastRequest = millis();
            return true;
        }
        return false;
    }
};

class BruteForceProtector {
private:
    static const uint8_t MAX_KEYS = 8;  // Track up to 8 different failure sources
    
    struct FailureRecord {
        char key[32];          // Identifier (IP, device ID, etc.)
        uint8_t failureCount = 0;
        unsigned long lastFailureTime = 0;
        unsigned long lockoutUntil = 0;
    };

    FailureRecord _records[MAX_KEYS];
    uint8_t _recordCount = 0;

public:
    BruteForceProtector() {
        memset(_records, 0, sizeof(_records));
    }

    /**
     * Check if an entity is currently locked out
     */
    bool isLockedOut(const char* key) {
        unsigned long now = millis();
        
        for (uint8_t i = 0; i < _recordCount; i++) {
            if (strcmp(_records[i].key, key) == 0) {
                if (now < _records[i].lockoutUntil) {
                    return true;  // Still locked out
                } else if (_records[i].lockoutUntil > 0) {
                    // Lockout has expired - reset
                    _records[i].failureCount = 0;
                    _records[i].lockoutUntil = 0;
                    return false;
                }
                return false;
            }
        }
        return false;
    }

    /**
     * Get remaining lockout time in milliseconds (0 if not locked)
     */
    unsigned long getRemainingLockout(const char* key) {
        unsigned long now = millis();
        
        for (uint8_t i = 0; i < _recordCount; i++) {
            if (strcmp(_records[i].key, key) == 0) {
                if (now < _records[i].lockoutUntil) {
                    return _records[i].lockoutUntil - now;
                }
            }
        }
        return 0;
    }

    /**
     * Record a failure for an entity
     * Returns true if allowed to continue, false if now locked out
     */
    bool recordFailure(const char* key) {
        unsigned long now = millis();
        
        // Find or create record
        int recordIdx = -1;
        for (uint8_t i = 0; i < _recordCount; i++) {
            if (strcmp(_records[i].key, key) == 0) {
                recordIdx = i;
                break;
            }
        }

        // Create new record if needed
        if (recordIdx == -1 && _recordCount < MAX_KEYS) {
            recordIdx = _recordCount++;
            strncpy(_records[recordIdx].key, key, sizeof(_records[recordIdx].key) - 1);
            _records[recordIdx].key[sizeof(_records[recordIdx].key) - 1] = '\0';
            _records[recordIdx].failureCount = 0;
        }

        if (recordIdx == -1) {
            return false;  // No space to track more sources
        }

        _records[recordIdx].failureCount++;
        _records[recordIdx].lastFailureTime = now;

        // Calculate lockout duration based on failure count
        unsigned long lockoutMs = 0;
        if (_records[recordIdx].failureCount >= 5) {
            lockoutMs = 600000;  // 10 minutes
        } else if (_records[recordIdx].failureCount >= 4) {
            lockoutMs = 120000;  // 2 minutes
        } else if (_records[recordIdx].failureCount >= 3) {
            lockoutMs = 30000;   // 30 seconds
        }

        if (lockoutMs > 0) {
            _records[recordIdx].lockoutUntil = now + lockoutMs;
            return false;  // Now locked out
        }

        return true;  // Still allowed
    }

    /**
     * Record a success - resets failure counter for an entity
     */
    void recordSuccess(const char* key) {
        for (uint8_t i = 0; i < _recordCount; i++) {
            if (strcmp(_records[i].key, key) == 0) {
                _records[i].failureCount = 0;
                _records[i].lockoutUntil = 0;
                _records[i].lastFailureTime = 0;
                return;
            }
        }
    }

    /**
     * Get current failure count for an entity
     */
    uint8_t getFailureCount(const char* key) {
        for (uint8_t i = 0; i < _recordCount; i++) {
            if (strcmp(_records[i].key, key) == 0) {
                return _records[i].failureCount;
            }
        }
        return 0;
    }

    /**
     * Clear all records
     */
    void reset() {
        memset(_records, 0, sizeof(_records));
        _recordCount = 0;
    }
};
