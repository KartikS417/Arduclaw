#pragma once
#include "BaseChannel.h"
#include "../core/MCUConfig.h"
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <cstring>

class SerialChannel : public BaseChannel {
private:
    HardwareSerial* _serial;
    char _buffer[512];      // Fixed char array instead of String
    uint16_t _bufferPos = 0;
    int _baudRate;

public:
    SerialChannel(int baudRate = 115200, int rxPin = 3, int txPin = 1)
        : _baudRate(baudRate), _serial(&Serial) {
        memset(_buffer, 0, sizeof(_buffer));
    }

    void begin() override {
        _serial->begin(_baudRate);
        _bufferPos = 0;
        memset(_buffer, 0, sizeof(_buffer));
    }

    void loop() override {
        while (_serial->available() && _bufferPos < sizeof(_buffer) - 1) {
            char c = _serial->read();
            if (c == '\n') {
                // Complete message
                break;
            } else if (c != '\r') {
                _buffer[_bufferPos++] = c;
            }
        }
    }

    bool available() override {
        return _bufferPos > 0;
    }

    String readMessage() override {
        String msg(_buffer);
        _bufferPos = 0;
        memset(_buffer, 0, sizeof(_buffer));
        return msg;
    }

    void sendMessage(const String& msg) override {
        StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
        doc["status"] = "success";
        doc["data"] = msg;
        serializeJsonPretty(doc, *_serial);
        _serial->println();
    }
};
