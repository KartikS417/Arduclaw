#pragma once
#include "BaseChannel.h"
#include "../core/MCUConfig.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>

#define HTTP_CHANNEL_MAX_QUEUE 8
#define HTTP_CHANNEL_MSG_SIZE 256

struct HTTPMessage {
    char data[HTTP_CHANNEL_MSG_SIZE];
};

class HTTPChannel : public BaseChannel {
private:
    String _endpoint;
    HTTPMessage _messageQueue[HTTP_CHANNEL_MAX_QUEUE];
    uint8_t _queueHead = 0;
    uint8_t _queueTail = 0;
    uint8_t _queueCount = 0;
    unsigned long _lastPoll = 0;
    unsigned long _pollInterval = 5000; // 5 seconds

public:
    HTTPChannel(const String& endpoint, unsigned long pollInterval = 5000)
        : _endpoint(endpoint), _pollInterval(pollInterval) {}

    void setEndpoint(const String& endpoint) {
        _endpoint = endpoint;
    }

    void setPollInterval(unsigned long interval) {
        _pollInterval = interval;
    }

    void begin() override {
        _lastPoll = millis();
        _queueHead = 0;
        _queueTail = 0;
        _queueCount = 0;
    }

    void loop() override {
        if (millis() - _lastPoll >= _pollInterval) {
            pollMessages();
            _lastPoll = millis();
        }
    }

    void pollMessages() {
        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);  // Configurable per MCU

        if (http.begin(_endpoint)) {
            int code = http.GET();
            if (code == 200) {
                String response = http.getString();
                StaticJsonDocument<JSON_BUFFER_SIZE_LG> doc;
                if (!deserializeJson(doc, response)) {
                    if (doc.containsKey("messages")) {
                        JsonArray messages = doc["messages"].as<JsonArray>();
                        for (JsonVariant msg : messages) {
                            // Enqueue message in fixed buffer
                            if (_queueCount < HTTP_CHANNEL_MAX_QUEUE) {
                                String msgStr = msg.as<String>();
                                strncpy(_messageQueue[_queueTail].data, 
                                       msgStr.c_str(), 
                                       HTTP_CHANNEL_MSG_SIZE - 1);
                                _messageQueue[_queueTail].data[HTTP_CHANNEL_MSG_SIZE - 1] = '\0';
                                _queueTail = (_queueTail + 1) % HTTP_CHANNEL_MAX_QUEUE;
                                _queueCount++;
                            }
                        }
                    }
                }
            }
            http.end();
        }
    }

    bool available() override {
        return _queueCount > 0;
    }

    String readMessage() override {
        if (_queueCount > 0) {
            String msg(_messageQueue[_queueHead].data);
            memset(_messageQueue[_queueHead].data, 0, HTTP_CHANNEL_MSG_SIZE);
            _queueHead = (_queueHead + 1) % HTTP_CHANNEL_MAX_QUEUE;
            _queueCount--;
            return msg;
        }
        return "";
    }

    void sendMessage(const String& msg) override {
        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);  // Configurable per MCU

        if (http.begin(_endpoint)) {
            http.addHeader("Content-Type", "application/json");
            StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
            doc["response"] = msg;
            String payload;
            serializeJson(doc, payload);
            int code = http.POST(payload);
            http.end();
        }
    }
};
