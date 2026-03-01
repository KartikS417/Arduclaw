#pragma once
#include <Arduino.h>
#include <SD.h>
#include <cstdio>

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

class Logger {
private:
    static Logger* _instance;
    LogLevel _level = LOG_INFO;
    bool _sdEnabled = false;
    bool _serialEnabled = true;
    unsigned long _startTime = 0;

    Logger() : _startTime(millis()) {}

public:
    static Logger& getInstance() {
        if (!_instance) {
            _instance = new Logger();
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
        _startTime = millis();
    }

    void setLevel(LogLevel level) {
        _level = level;
    }

    void log(LogLevel level, const String& tag, const String& msg) {
        if (level < _level) return;

        const char* levelStr;
        switch (level) {
            case LOG_DEBUG:
                levelStr = "DEBUG";
                break;
            case LOG_INFO:
                levelStr = "INFO ";
                break;
            case LOG_WARN:
                levelStr = "WARN ";
                break;
            case LOG_ERROR:
                levelStr = "ERROR";
                break;
            default:
                levelStr = "UNKN ";
        }

        unsigned long elapsed = millis() - _startTime;
        
        // Use fixed buffer instead of String concatenation
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), 
                 "[%lu ms] [%s] [%s] %s",
                 elapsed, levelStr, tag.c_str(), msg.c_str());

        // Serial output
        if (_serialEnabled) {
            Serial.println(logBuffer);
        }

        // SD card output
        if (_sdEnabled) {
            File f = SD.open("/logs.txt", FILE_APPEND);
            if (f) {
                f.println(logBuffer);
                f.close();
            }
        }
    }

    // Convenience methods
    void debug(const String& tag, const String& msg) {
        log(LOG_DEBUG, tag, msg);
    }

    void info(const String& tag, const String& msg) {
        log(LOG_INFO, tag, msg);
    }

    void warn(const String& tag, const String& msg) {
        log(LOG_WARN, tag, msg);
    }

    void error(const String& tag, const String& msg) {
        log(LOG_ERROR, tag, msg);
    }
};

inline Logger* Logger::_instance = nullptr;

// Global convenience macros
#define LOG_DEBUG_TAG(tag, msg) Logger::getInstance().debug(tag, msg)
#define LOG_INFO_TAG(tag, msg) Logger::getInstance().info(tag, msg)
#define LOG_WARN_TAG(tag, msg) Logger::getInstance().warn(tag, msg)
#define LOG_ERROR_TAG(tag, msg) Logger::getInstance().error(tag, msg)
