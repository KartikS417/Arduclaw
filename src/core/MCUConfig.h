#pragma once

/**
 * MCU Target Configuration
 * 
 * Compile-time configuration based on target MCU for optimal memory usage
 * Add -DTARGET_MCU=MCU_ESP32_S3 to platformio.ini or compiler flags
 */

#define MCU_ESP32_S3    1
#define MCU_ESP32_C3    2
#define MCU_ESP8266     3
#define MCU_ESP32       4  // Default ESP32

// Auto-detect or use provided target
#ifndef TARGET_MCU
  #ifdef ARDUINO_ESP32S3_DEV
    #define TARGET_MCU MCU_ESP32_S3
  #elif defined(ARDUINO_ESP32C3_DEV)
    #define TARGET_MCU MCU_ESP32_C3
  #elif defined(ESP8266)
    #define TARGET_MCU MCU_ESP8266
  #else
    #define TARGET_MCU MCU_ESP32  // Default
  #endif
#endif

// ============================================
// ESP32-S3: 8MB Flash, 8MB PSRAM, 512KB SRAM
// ============================================
#if TARGET_MCU == MCU_ESP32_S3
  #define MAX_PROVIDERS           8
  #define MAX_CHANNELS            6
  #define MAX_TRACKED_REQUESTS    128
  #define MAX_ACTION_REGISTRY     64
  #define JSON_BUFFER_SIZE_LG     4096  // Large (responses)
  #define JSON_BUFFER_SIZE_MD     2048  // Medium (processing)
  #define JSON_BUFFER_SIZE_SM     512   // Small (validation)
  #define REQUEST_QUEUE_MAX       32
  #define EVENT_BUFFER_MAX        256
  #define STRING_BUFFER_SIZE      1024
  #define MAX_RETRY_ATTEMPTS      3
  #define HTTP_TIMEOUT_MS         15000
  #define PROVIDER_STACK_SIZE     8192

// ============================================
// ESP32-C3: 4MB Flash, No PSRAM, 400KB SRAM
// ============================================
#elif TARGET_MCU == MCU_ESP32_C3
  #define MAX_PROVIDERS           4
  #define MAX_CHANNELS            3
  #define MAX_TRACKED_REQUESTS    64
  #define MAX_ACTION_REGISTRY     32
  #define JSON_BUFFER_SIZE_LG     2048  // Large responses limited
  #define JSON_BUFFER_SIZE_MD     1024  // Medium
  #define JSON_BUFFER_SIZE_SM     256   // Small
  #define REQUEST_QUEUE_MAX       16
  #define EVENT_BUFFER_MAX        128
  #define STRING_BUFFER_SIZE      512
  #define MAX_RETRY_ATTEMPTS      2
  #define HTTP_TIMEOUT_MS         10000
  #define PROVIDER_STACK_SIZE     4096

// ============================================
// ESP8266: 4MB Flash, No PSRAM, 160KB SRAM
// ============================================
#elif TARGET_MCU == MCU_ESP8266
  #define MAX_PROVIDERS           2
  #define MAX_CHANNELS            2
  #define MAX_TRACKED_REQUESTS    32
  #define MAX_ACTION_REGISTRY     16
  #define JSON_BUFFER_SIZE_LG     1024  // Very constrained
  #define JSON_BUFFER_SIZE_MD     512
  #define JSON_BUFFER_SIZE_SM     128
  #define REQUEST_QUEUE_MAX       8
  #define EVENT_BUFFER_MAX        64
  #define STRING_BUFFER_SIZE      256
  #define MAX_RETRY_ATTEMPTS      1
  #define HTTP_TIMEOUT_MS         8000
  #define PROVIDER_STACK_SIZE     2048

// ============================================
// Default ESP32 (Generic): 4MB Flash, 320KB SRAM
// ============================================
#else
  #define MAX_PROVIDERS           4
  #define MAX_CHANNELS            4
  #define MAX_TRACKED_REQUESTS    64
  #define MAX_ACTION_REGISTRY     32
  #define JSON_BUFFER_SIZE_LG     2048
  #define JSON_BUFFER_SIZE_MD     1024
  #define JSON_BUFFER_SIZE_SM     256
  #define REQUEST_QUEUE_MAX       16
  #define EVENT_BUFFER_MAX        128
  #define STRING_BUFFER_SIZE      512
  #define MAX_RETRY_ATTEMPTS      2
  #define HTTP_TIMEOUT_MS         10000
  #define PROVIDER_STACK_SIZE     4096
#endif

// Feature enable/disable for memory optimization
#if TARGET_MCU == MCU_ESP8266
  #define ENABLE_SD_LOGGING       0  // Disable SD for 8266
  #define ENABLE_PSRAM_USAGE      0
  #define ENABLE_LARGE_JSON       0  // Disable large JSON buffers
#else
  #define ENABLE_SD_LOGGING       1
  #define ENABLE_PSRAM_USAGE      1
  #define ENABLE_LARGE_JSON       1
#endif

// Static allocation helpers
#define STATIC_ARRAY_IMPL(type, name, size) \
    type name##_storage[size]; \
    uint8_t name##_count = 0

// Debug helper to show configuration
#define SHOW_MCU_CONFIG() \
    do { \
        Serial.println("[MCU Config]"); \
        Serial.println("  Providers: " #MAX_PROVIDERS); \
        Serial.println("  Channels: " #MAX_CHANNELS); \
        Serial.println("  JSON Large: " #JSON_BUFFER_SIZE_LG); \
        Serial.println("  Request Queue: " #REQUEST_QUEUE_MAX); \
    } while(0)
