#pragma once

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
    #include <esp_task_wdt.h>
    #define HAS_WATCHDOG 1
#elif defined(ARDUINO_ARCH_ESP8266)
    // ESP8266 has watchdog but different API
    // For simplicity, yield() which feeds the SW watchdog
    #define HAS_WATCHDOG 1
#else
    #define HAS_WATCHDOG 0
#endif

/**
 * Watchdog Management for Async Operations
 * 
 * Prevents device resets during long-running non-blocking operations.
 * 
 * ESP32: Uses TWDT (Task Watchdog Timer) - call esp_task_wdt_reset() periodically
 * ESP8266: Uses Software Watchdog - call yield() periodically
 * 
 * Usage:
 *   while (processing) {
 *       WatchdogHelper::feed();  // Feed watchdog every 100-500ms
 *       doSomeWork();
 *   }
 */

class WatchdogHelper {
private:
    static unsigned long _lastFeedTime;
    static const unsigned long FEED_INTERVAL = 500;  // Feed every 500ms

public:
    /**
     * Initialize watchdog monitoring
     * Call in setup()
     */
    static void begin() {
        #if defined(ARDUINO_ARCH_ESP32)
            // Subscribe this task to TWDT
            #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
                esp_task_wdt_config_t twdt_config = {
                    .timeout_ms = 10000,
                    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
                    .trigger_panic = true,
                };
                esp_task_wdt_init(&twdt_config);
            #else
                esp_task_wdt_init(10, true);  // 10 second timeout
            #endif
            esp_task_wdt_add(NULL); // Add current task
        #endif
        _lastFeedTime = millis();
    }

    /**
     * Feed watchdog if interval has elapsed
     * Non-blocking - returns immediately if not time yet
     * Safe to call frequently without performance impact
     */
    static void feed() {
        unsigned long now = millis();
        if (now - _lastFeedTime >= FEED_INTERVAL) {
            _feedInternal();
            _lastFeedTime = now;
        }
    }

    /**
     * Force watchdog feed immediately
     * Use if you know you're about to block or process heavily
     */
    static void feedNow() {
        _feedInternal();
        _lastFeedTime = millis();
    }

    /**
     * Safe delay that feeds watchdog
     * 
     * Example:
     *   WatchdogHelper::delayWithFeed(100);  // 100ms with watchdog protection
     */
    static void delayWithFeed(unsigned long delayMs) {
        unsigned long endTime = millis() + delayMs;
        while (millis() < endTime) {
            feed();
            yield();  // Let other tasks run
        }
    }

    /**
     * Run a function with periodic watchdog feeding
     * Calls function repeatedly, feeding watchdog between iterations
     * 
     * Example:
     *   WatchdogHelper::protectedLoop([&]() {
     *       return httpClient.process();  // Returns true when complete
     *   }, 5000);  // Max 5 second timeout
     */
    static bool protectedLoop(std::function<bool()> fn, unsigned long timeoutMs = 10000) {
        unsigned long startTime = millis();
        
        while (millis() - startTime < timeoutMs) {
            feed();
            
            // Call the function
            if (fn()) {
                return true;  // Function completed successfully
            }
            
            yield();  // Let other tasks/interrupts run
        }
        
        return false;  // Timeout
    }

    /**
     * Get time remaining before watchdog timeout
     * Returns milliseconds (0 if disabled/no timeout)
     */
    static unsigned long getTimeToTimeout() {
        #if defined(ARDUINO_ARCH_ESP32)
            // ESP32 TWDT timeout is 10 seconds
            return 10000;
        #elif defined(ARDUINO_ARCH_ESP8266)
            // ESP8266 software watchdog is ~1.8 seconds
            return 1800;
        #else
            return 0;
        #endif
    }

    /**
     * Check if watchdog is running
     */
    static bool isEnabled() {
        return HAS_WATCHDOG == 1;
    }

private:
    static void _feedInternal() {
        #if defined(ARDUINO_ARCH_ESP32)
            esp_task_wdt_reset();
        #elif defined(ARDUINO_ARCH_ESP8266)
            ESP.wdtFeed();
        #endif
    }
};

// Initialize static member
inline unsigned long WatchdogHelper::_lastFeedTime = 0;

/**
 * RAII Helper - Automatically feeds watchdog during scope lifetime
 * 
 * Example:
 *   {
 *       WatchdogGuard guard;  // Start watchdog feeding
 *       doLongOperation();
 *   }  // Automatic cleanup, stops feeding
 */
class WatchdogGuard {
public:
    WatchdogGuard() {
        WatchdogHelper::feedNow();
    }

    ~WatchdogGuard() {
        WatchdogHelper::feedNow();
    }

    // Feed periodically during guard lifetime
    void tick() {
        WatchdogHelper::feed();
    }
};
