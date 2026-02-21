#pragma once
#include "BaseChannel.h"
#include "../core/MCUConfig.h"
#include "../core/MQTTSecurity.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

class MQTTChannel : public BaseChannel {
private:
    PubSubClient _client;
    String _broker;
    int _port;
    String _clientId;
    String _topic;
    String _lastMessage;
    String _secretKey;  // 64-char hex HMAC-SHA256 key
    bool _connected = false;
    bool _securityEnabled = false;

    static void staticMqttCallback(char* topic, byte* payload, unsigned int length) {
        // Static callback that can be customized or routed to instance
    }

public:
    MQTTChannel(const String& broker, int port, const String& clientId, const String& topic)
        : _broker(broker), _port(port), _clientId(clientId), _topic(topic),
          _client(WiFiClient()) {}

    void setTopic(const String& topic) {
        _topic = topic;
    }

    void setSecretKey(const String& hexKey) {
        _secretKey = hexKey;
        _securityEnabled = (hexKey.length() == 64);  // 64 hex chars = 32 bytes
    }

    void begin() override {
        _client.setServer(_broker.c_str(), _port);
        _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
            if (_securityEnabled) {
                // Expected format: {"data":"...", "signature":"..."}
                StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
                String rawPayload = String((char*)payload).substring(0, length);
                
                if (deserializeJson(doc, rawPayload) == 0) {
                    String signature = doc["signature"].as<String>();
                    String data = doc["data"].as<String>();
                    
                    if (MQTTSecurity::verifyWithHexKey(_secretKey, data.c_str(), data.length(), signature)) {
                        _lastMessage = data;
                    } else {
                        // Signature verification failed - discard message
                        _lastMessage = "";
                    }
                } else {
                    _lastMessage = "";
                }
            } else {
                // No security enabled - accept raw message
                _lastMessage = String((char*)payload).substring(0, length);
            }
        });
        connect();
    }

    void connect() {
        if (_client.connect(_clientId.c_str())) {
            _connected = true;
            _client.subscribe(_topic.c_str());
        }
    }

    void loop() override {
        if (!_client.connected()) {
            connect();
        } else {
            _client.loop();
        }
    }

    bool available() override {
        return _lastMessage.length() > 0;
    }

    String readMessage() override {
        String msg = _lastMessage;
        _lastMessage = "";
        return msg;
    }

    void sendMessage(const String& msg) override {
        if (_client.connected()) {
            StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
            doc["status"] = "success";
            doc["data"] = msg;
            
            if (_securityEnabled) {
                // Sign the data field
                String signature = MQTTSecurity::signWithHexKey(_secretKey, msg.c_str(), msg.length());
                if (signature.length() > 0) {
                    doc["signature"] = signature;
                }
            }
            
            String payload;
            serializeJson(doc, payload);
            _client.publish(_topic.c_str(), payload.c_str());
        }
    }
};
