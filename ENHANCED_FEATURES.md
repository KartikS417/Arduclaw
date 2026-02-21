// ArduClaw Enhanced Features - Quick Reference

/**
 * ==========================================
 * 1. STRUCTURED LOGGING
 * ==========================================
 * 
 * Framework provides multi-level structured logging with context:
 * - LOG_DEBUG_TAG, LOG_INFO_TAG, LOG_WARN_TAG, LOG_ERROR_TAG
 * - Events logged to Serial and SPIFFS
 * 
 * Example:
 *   LOG_INFO_TAG("MyTag", "This is a message");
 *   
 * Event logging with structured data:
 *   LogEvent evt(EventType::PROVIDER_REQUEST_START, ErrorCode::SUCCESS,
 *                "Provider", "Starting request");
 *   evt.context = "prompt=hello";
 *   StructuredLogger::getInstance().logEvent(evt);
 */

/**
 * ==========================================
 * 2. ERROR CODES
 * ==========================================
 * 
 * Comprehensive error code enum (ErrorCodes.h):
 * 
 * Provider Errors (100-199):
 *   - PROVIDER_NOT_FOUND, PROVIDER_BUSY, PROVIDER_TIMEOUT, PROVIDER_FAILED
 * 
 * Network Errors (200-299):
 *   - NETWORK_ERROR, HTTP_ERROR, HTTP_TIMEOUT, DNS_FAILED
 * 
 * Request Errors (300-399):
 *   - INVALID_REQUEST, INVALID_RESPONSE, RATE_LIMIT_EXCEEDED, MAX_RETRIES_EXCEEDED
 * 
 * JSON Errors (400-499):
 *   - JSON_PARSE_ERROR, JSON_VALIDATION_ERROR, MISSING_FIELD
 * 
 * Helper Methods:
 *   - ErrorCodeHelper::toString(code)
 *   - ErrorCodeHelper::isRetryable(code)
 *   - ErrorCodeHelper::isSuccess(code)
 */

/**
 * ==========================================
 * 3. REQUEST TRACKING (Non-blocking Model)
 * ==========================================
 * 
 * RequestTracker provides visibility into active requests:
 * 
 * Example:
 *   // Query request status
 *   RequestStatus* status = providerManager.getRequestStatus(requestId);
 *   if (status) {
 *     Serial.println(status->stateToString());           // "IN_PROGRESS"
 *     Serial.println(status->getElapsedTime());          // ms elapsed
 *     Serial.println(status->result);                    // Result string
 *   }
 *   
 *   // Get stats on all requests
 *   String stats = RequestTracker::getInstance().getStats();
 *   // Output: "Requests: 5 (active:2 success:2 failed:1 timeout:0)"
 */

/**
 * ==========================================
 * 4. TIMEOUT & RETRY LOGIC
 * ==========================================
 * 
 * RequestConfig struct controls retry behavior:
 * 
 *   RequestConfig config;
 *   config.timeoutMs = 15000;           // 15 second timeout
 *   config.maxRetries = 3;              // Retry up to 3 times
 *   config.retryDelayMs = 2000;         // 2 second delay between retries
 *   config.exponentialBackoff = true;   // Exponential backoff (2^attempt)
 * 
 * Usage:
 *   int requestId = providerManager.sendAsyncWithRetry(
 *     prompt,
 *     [](String result, ErrorCode code) {
 *       Serial.println("Success: " + result);
 *     },
 *     [](ErrorCode code, const String& error) {
 *       Serial.println("Error: " + ErrorCodeHelper::toString(code));
 *     },
 *     config  // Custom config
 *   );
 */

/**
 * ==========================================
 * 5. REQUEST STATE EXPOSURE
 * ==========================================
 * 
 * RequestState enum:
 *   - PENDING: Queued, waiting to start
 *   - IN_PROGRESS: Currently being processed
 *   - SUCCESS: Completed successfully
 *   - FAILED: Failed after retries
 *   - TIMEOUT: Exceeded timeout threshold
 *   - CANCELLED: Manually cancelled
 * 
 * Check request state:
 *   RequestStatus* status = providerManager.getRequestStatus(requestId);
 *   if (status->state == RequestState::SUCCESS) {
 *     // Process result
 *   } else if (status->isComplete()) {
 *     // Request finished (success or failure)
 *   }
 * 
 * State change callbacks:
 *   RequestTracker::getInstance().setStateChangeCallback(
 *     [](const RequestStatus& status) {
 *       Serial.println("Request " + String(status.requestId) + 
 *                     " is now " + status.stateToString());
 *     }
 *   );
 */

/**
 * ==========================================
 * 6. ASYNC REQUEST QUEUE
 * ==========================================
 * 
 * Automatically handles:
 * - Multiple concurrent requests
 * - Retry logic with exponential backoff
 * - Timeout detection
 * - Queue depth management
 * 
 * Queue is processed in providerManager.loop():
 *   void loop() {
 *     providerManager.loop();  // Processes async queue
 *   }
 */

/**
 * ==========================================
 * COMPLETE EXAMPLE USAGE
 * ==========================================
 */

#include <Arduino.h>
#include "Arduclaw.h"
#include "core/Logger.h"
#include "core/StructuredLogger.h"
#include "core/RequestTracker.h"

ArduClaw arduclaw;

void setup() {
  Serial.begin(115200);
  
  // Initialize logging
  Logger::getInstance().begin(true);
  StructuredLogger::getInstance().begin(true);
  
  // Subscribe to state changes
  RequestTracker::getInstance().setStateChangeCallback(
    [](const RequestStatus& status) {
      if (status.isComplete()) {
        if (status.state == RequestState::SUCCESS) {
          Serial.println("✓ Request " + String(status.requestId) + " SUCCESS");
        } else if (status.state == RequestState::TIMEOUT) {
          Serial.println("✗ Request " + String(status.requestId) + " TIMEOUT");
        } else if (status.state == RequestState::FAILED) {
          Serial.println("✗ Request " + String(status.requestId) + " FAILED");
        }
      }
    }
  );
  
  arduclaw.begin();
}

void loop() {
  // Call provider.loop() to process async queue
  arduclaw.loop();
  
  // Monitor request stats
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 5000) {
    lastLog = millis();
    Serial.println(RequestTracker::getInstance().getStats());
    Serial.println(arduclaw.providerManager.getStatus());
  }
}

/**
 * ==========================================
 * EVENT TYPES (for StructuredLogger)
 * ==========================================
 * 
 * EventType enum includes:
 *   - PROVIDER_REQUEST_START
 *   - PROVIDER_REQUEST_SUCCESS
 *   - PROVIDER_REQUEST_FAILURE
 *   - PROVIDER_TIMEOUT
 *   - PROVIDER_RETRY
 *   - PROVIDER_SWITCH
 *   - CONFIG_LOAD / CONFIG_SAVE / CONFIG_INVALID
 *   - CHANNEL_MESSAGE_RECEIVED / CHANNEL_MESSAGE_SENT
 *   - MEMORY_WARNING / MEMORY_CRITICAL
 *   - NETWORK_CONNECTED / NETWORK_DISCONNECTED
 *   - JSON_PARSE_ERROR / JSON_VALIDATION_ERROR
 *   - RATE_LIMIT_HIT
 */
