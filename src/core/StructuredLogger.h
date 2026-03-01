#pragma once
#include <Arduino.h>
#include <vector>            // std::vector for buffering events
#include <SD.h>              // SD and File types for log persistence
#include "ErrorCodes.h"
#include "Logger.h"         // LogLevel enum and LOG_* constants

enum class EventType {
    PROVIDER_REQUEST_START,
    PROVIDER_REQUEST_SUCCESS,
    PROVIDER_REQUEST_FAILURE,
    PROVIDER_TIMEOUT,
    PROVIDER_RETRY,
    PROVIDER_SWITCH,
    
    CONFIG_LOAD,
    CONFIG_SAVE,
    CONFIG_INVALID,
    
    CHANNEL_MESSAGE_RECEIVED,
    CHANNEL_MESSAGE_SENT,
    CHANNEL_ERROR,
    
    MEMORY_WARNING,
    MEMORY_CRITICAL,
    
    RATE_LIMIT_HIT,
    
    JSON_PARSE_ERROR,
    JSON_VALIDATION_ERROR,
    
    NETWORK_CONNECTED,
    NETWORK_DISCONNECTED,
    
    SYSTEM_ERROR
};

struct LogEvent {
    EventType type;
    ErrorCode code;
    unsigned long timestamp;
    String tag;
    String message;
    String context;  // Additional contextual data as JSON
    
    LogEvent(EventType t, ErrorCode c, const String& tg, const String& msg)
        : type(t), code(c), timestamp(millis()), tag(tg), message(msg), context("") {}
    
    String toString() const {
        return String("[") + timestamp + "ms] [" + tag + "] " + message + 
               " [" + ErrorCodeHelper::toString(code) + "]" +
               (context.length() > 0 ? " {" + context + "}" : "");
    }
};

class StructuredLogger {
private:
    static StructuredLogger* _instance;
    LogLevel _level;
    bool _sdEnabled = false;
    bool _serialEnabled = true;
    std::vector<LogEvent> _eventBuffer;
    static const int MAX_BUFFER_SIZE = 256;
    
    StructuredLogger() : _level(LOG_INFO) {}

public:
    static StructuredLogger& getInstance() {
        if (!_instance) {
            _instance = new StructuredLogger();
        }
        return *_instance;
    }

    void begin(bool useSerial = true, int sdCsPin = -1) {
        _serialEnabled = useSerial;
        if (useSerial) {
            Serial.begin(115200);
        }
        if (sdCsPin >= 0) {
            _sdEnabled = SD.begin(sdCsPin);
        }
    }

    void logEvent(const LogEvent& event) {
        // Add to buffer
        if (_eventBuffer.size() >= MAX_BUFFER_SIZE) {
            _eventBuffer.erase(_eventBuffer.begin());
        }
        _eventBuffer.push_back(event);

        // Determine log level from event
        LogLevel eventLevel = LOG_INFO;
        if (event.code == ErrorCode::SUCCESS) {
            eventLevel = LOG_DEBUG;
        } else if ((int)event.code >= 600) {
            eventLevel = LOG_ERROR;
        } else if ((int)event.code >= 400) {
            eventLevel = LOG_WARN;
        }

        if (eventLevel < _level) return;

        String output = event.toString();
        
        // Serial output
        if (_serialEnabled) {
            Serial.println(output);
        }

        // SD output
        if (_sdEnabled) {
            File f = SD.open("/events.log", FILE_APPEND);
            if (f) {
                f.println(output);
                f.close();
            }
        }
    }

    void logWithContext(EventType type, ErrorCode code, const String& tag, 
                       const String& message, const String& context) {
        LogEvent event(type, code, tag, message);
        event.context = context;
        logEvent(event);
    }

    const std::vector<LogEvent>& getEventBuffer() const {
        return _eventBuffer;
    }

    void clearEventBuffer() {
        _eventBuffer.clear();
    }

    int getEventCount(EventType type) const {
        int count = 0;
        for (const auto& event : _eventBuffer) {
            if (event.type == type) count++;
        }
        return count;
    }
};

inline StructuredLogger* StructuredLogger::_instance = nullptr;

// Convenience macro
#define LOG_EVENT(type, code, tag, msg) \
    StructuredLogger::getInstance().logEvent(LogEvent(type, code, tag, msg))

#define LOG_EVENT_CTX(type, code, tag, msg, ctx) \
    StructuredLogger::getInstance().logWithContext(type, code, tag, msg, ctx)
