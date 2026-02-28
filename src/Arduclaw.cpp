#include "ArduClaw.h"
#include "core/Logger.h"
#include "core/StructuredLogger.h"
#include "core/RequestTracker.h"
#include "core/ErrorCodes.h"
#include "core/MCUConfig.h"
#include "core/WatchdogHelper.h"

static bool extractJsonObject(const String& input, String& outJson) {
    int start = input.indexOf('{');
    if (start == -1) {
        LOG_WARN_TAG("ArduClaw", "No JSON object found in response");
        return false;
    }

    int braceCount = 0;

    for (int i = start; i < input.length(); i++) {
        if (input[i] == '{') braceCount++;
        if (input[i] == '}') braceCount--;

        if (braceCount == 0 && i > start) {
            outJson = input.substring(start, i + 1);
            return true;
        }
    }

    LOG_WARN_TAG("ArduClaw", "Unbalanced braces in JSON");
    return false;
}

void ArduClaw::ask(String msg,
                   std::function<void(JsonDocument&)> cb) {

    // Check for brute-force lockout (use "global" as default source)
    if (bruteForceProtector.isLockedOut("global")) {
        unsigned long remainingMs = bruteForceProtector.getRemainingLockout("global");
        LOG_WARN_TAG("ArduClaw", "Brute-force lockout active for " + String(remainingMs / 1000) + "s");
        LogEvent evt(EventType::PROVIDER_REQUEST_FAILURE, ErrorCode::RATE_LIMIT_EXCEEDED,
                    "ArduClaw", "Brute-force lockout in effect");
        StructuredLogger::getInstance().logEvent(evt);
        return;
    }

    String prompt = PromptManager::build(msg);

    BaseProvider* provider = providerManager.getActiveProvider();
    if (!provider) {
        LOG_ERROR_TAG("ArduClaw", "No active provider");
        bruteForceProtector.recordFailure("global");  // Record failure
        LogEvent evt(EventType::PROVIDER_REQUEST_FAILURE, ErrorCode::PROVIDER_NOT_FOUND,
                    "ArduClaw", "No active provider");
        StructuredLogger::getInstance().logEvent(evt);
        return;
    }

    // Use enhanced sendAsyncWithRetry for better error handling
    RequestConfig config(10000, 2);
    
    int requestId = providerManager.sendAsyncWithRetry(
        prompt,
        [this, cb](String response, ErrorCode code) {
            String cleanJson;

            if (!extractJsonObject(response, cleanJson)) {
                LOG_ERROR_TAG("ArduClaw", "Failed to extract JSON from response");
                LogEvent evt(EventType::JSON_PARSE_ERROR, ErrorCode::JSON_PARSE_ERROR,
                           "ArduClaw", "JSON extraction failed");
                StructuredLogger::getInstance().logEvent(evt);
                providerManager.markFailure();
                bruteForceProtector.recordFailure("global");  // Record failure
                return;
            }

            StaticJsonDocument<JSON_BUFFER_SIZE_MD> doc;

            if (JsonValidator::validate(
                    cleanJson,
                    registry,
                    doc,
                    runtimePermission)) {

                providerManager.markSuccess();
                bruteForceProtector.recordSuccess("global");  // Reset on success
                LOG_DEBUG_TAG("ArduClaw", "Response validated");

                if (_actionHandler) {
                    _actionHandler(doc);
                }

                if (cb) {
                    cb(doc);
                }

            } else {
                LOG_WARN_TAG("ArduClaw", "JSON validation failed");
                LogEvent evt(EventType::JSON_VALIDATION_ERROR, ErrorCode::JSON_VALIDATION_ERROR,
                           "ArduClaw", "Validation failed");
                StructuredLogger::getInstance().logEvent(evt);
                providerManager.markFailure();
                bruteForceProtector.recordFailure("global");  // Record failure
            }
        },
        [this](ErrorCode code, const String& error) {
            LOG_ERROR_TAG("ArduClaw", "Provider error: " + error);
            LogEvent evt(EventType::PROVIDER_REQUEST_FAILURE, code,
                        "ArduClaw", error);
            StructuredLogger::getInstance().logEvent(evt);
            providerManager.markFailure();
            bruteForceProtector.recordFailure("global");  // Record failure
        },
        config
    );

    LOG_INFO_TAG("ArduClaw", "Request enqueued with ID: " + String(requestId));
}

void ArduClaw::stream(String msg,
                      std::function<void(const String& chunk)> onChunk,
                      std::function<void(ErrorCode code)> onComplete,
                      std::function<void(ErrorCode code, const String& error)> onError) {

    // Check for brute-force lockout
    if (bruteForceProtector.isLockedOut("global_stream")) {
        unsigned long remainingMs = bruteForceProtector.getRemainingLockout("global_stream");
        LOG_WARN_TAG("ArduClaw", "Brute-force lockout active for streaming: " + String(remainingMs / 1000) + "s");
        if (onError) onError(ErrorCode::RATE_LIMIT_EXCEEDED, "Brute-force lockout");
        return;
    }

    String prompt = PromptManager::build(msg);

    BaseProvider* provider = providerManager.getActiveProvider();
    if (!provider) {
        LOG_ERROR_TAG("ArduClaw", "No active provider for streaming");
        bruteForceProtector.recordFailure("global_stream");
        if (onError) onError(ErrorCode::PROVIDER_NOT_FOUND, "No active provider");
        return;
    }

    // Use a default config for streaming. Longer timeout, fewer retries.
    RequestConfig config(20000, 1);

    providerManager.streamAsync(
        prompt,
        [this, onChunk](const String& chunk) {
            // Pass chunk directly to user callback
            if (onChunk) onChunk(chunk);
        },
        [this, onComplete](ErrorCode code) {
            // Stream completed successfully
            bruteForceProtector.recordSuccess("global_stream");
            providerManager.markSuccess();
            if (onComplete) onComplete(code);
        },
        [this, onError](ErrorCode code, const String& error) {
            // Stream failed
            LOG_ERROR_TAG("ArduClaw", "Provider stream error: " + error);
            bruteForceProtector.recordFailure("global_stream");
            providerManager.markFailure();
            if (onError) onError(code, error);
        },
        config);
}

void ArduClaw::loop()
{
    WatchdogHelper::feed();  // Feed watchdog during main loop processing

    if (!providerManager.hasProvider()) {
        return;
    }

    // Allow providers to process async HTTP / state machines
    providerManager.loop();

    // Allow channels (MQTT, Telegram, WhatsApp, etc.) to process incoming data
    channelManager.loop();

    BaseChannel** channels = channelManager.get();
    uint8_t channelCount = channelManager.count();

    for (uint8_t i = 0; i < channelCount; i++)
    {
        BaseChannel* ch = channels[i];
        
        if (!ch)
            continue;

        if (!ch->available())
            continue;

        // Prevent overlapping LLM requests - check async queue
        if (RequestTracker::getInstance().getActiveRequestCount() > 0)
            continue;

        String msg = ch->readMessage();

        if (msg.length() == 0)
            continue;

        // Send to LLM asynchronously
        ask(msg, [this, ch](JsonDocument& doc)
        {
            if (!ch)
                return;

            // You can customize response handling here
            if (doc.containsKey("response"))
            {
                ch->sendMessage(doc["response"].as<String>());
            }
            else
            {
                ch->sendMessage("Action executed");
            }
        });

        // Process one message per loop iteration
        break;
    }
}
