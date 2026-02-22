#pragma once

#include <Arduino.h>

struct DeviceConfig {
    String wifi_ssid;
    String wifi_pass;

    String llm_host;
    int    llm_port;
    String llm_endpoint;
    String llm_model;

    String mqtt_host;
    int    mqtt_port;
    String mqtt_topic;
    String mqtt_secret_key;  // 32-byte hex string for HMAC-SHA256 signing
};

class ConfigManagerClass {
private:
    bool validateConfig();
    String configPath = "/device_config.json";

public:
    bool begin();
    bool load();
    bool save();
    bool isValid();
    bool validate();

    void setConfigPath(const String& path) {
        configPath = path;
    }

    void startPortalAsync();
    void stopPortal();

    DeviceConfig config;
};

extern ConfigManagerClass ConfigManager;
