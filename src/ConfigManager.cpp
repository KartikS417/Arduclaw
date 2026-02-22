#include "ConfigManager.h"
#include "core/Logger.h"
#include "core/MCUConfig.h"
#include "core/SecretManager.h"
#include "core/ConfigIntegrity.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

WebServer server(80);
static bool portalRunning = false;
static TaskHandle_t portalTaskHandle = NULL;

ConfigManagerClass ConfigManager;

bool ConfigManagerClass::begin() {
    bool started = SPIFFS.begin(true);
    if (started) {
        LOG_INFO_TAG("ConfigManager", "SPIFFS initialized");
    } else {
        LOG_ERROR_TAG("ConfigManager", "SPIFFS initialization failed");
    }
    return started;
}

bool ConfigManagerClass::validateConfig() {
    if (config.wifi_ssid.length() == 0) {
        LOG_WARN_TAG("ConfigManager", "WiFi SSID is empty");
        return false;
    }
    if (config.llm_host.length() == 0) {
        LOG_WARN_TAG("ConfigManager", "LLM host is empty");
        return false;
    }
    if (config.llm_port < 1 || config.llm_port > 65535) {
        LOG_WARN_TAG("ConfigManager", "Invalid LLM port");
        return false;
    }
    return true;
}

bool ConfigManagerClass::validate() {
    return validateConfig();
}

bool ConfigManagerClass::isValid() {
    return validateConfig();
}

bool ConfigManagerClass::load() {
    File file = SPIFFS.open(configPath, "r");
    if (!file) {
        LOG_WARN_TAG("ConfigManager", "Config file not found");
        return false;
    }

    StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
    if (deserializeJson(doc, file)) {
        file.close();
        LOG_ERROR_TAG("ConfigManager", "JSON deserialization failed");
        return false;
    }
    file.close();

    // Verify config integrity if hash present
    String storedHash = doc["_integrity_hash"].as<String>();
    if (storedHash.length() > 0) {
        // Create a copy for integrity check (without hash)
        StaticJsonDocument<JSON_BUFFER_SIZE_SM> docForVerify;
        docForVerify.set(doc);
        docForVerify.remove("_integrity_hash");
        String docStr;
        serializeJson(docForVerify, docStr);
        
        if (!ConfigIntegrity::verify(docStr, storedHash)) {
            LOG_ERROR_TAG("ConfigManager", "Config integrity check FAILED - possible tampering");
            return false;  // Reject tampered config
        }
        LOG_DEBUG_TAG("ConfigManager", "Config integrity verified");
    }

    config.wifi_ssid = doc["wifi_ssid"].as<String>();
    config.wifi_pass = doc["wifi_pass"].as<String>();

    config.llm_host  = doc["llm_host"].as<String>();
    config.llm_port  = doc["llm_port"] | 8000;  // Default port 8000
    config.llm_endpoint = doc["llm_endpoint"].as<String>();
    config.llm_model = doc["llm_model"].as<String>();

    config.mqtt_host = doc["mqtt_host"].as<String>();
    config.mqtt_port = doc["mqtt_port"] | 1883;  // Default MQTT port
    config.mqtt_topic = doc["mqtt_topic"].as<String>();
    
    // Decrypt MQTT secret key if encrypted
    String encryptedKey = doc["mqtt_secret_key"].as<String>();
    if (encryptedKey.length() > 0) {
        if (SecretManager::isEncrypted(encryptedKey)) {
            config.mqtt_secret_key = SecretManager::decrypt(encryptedKey);
            LOG_DEBUG_TAG("ConfigManager", "MQTT secret key decrypted");
        } else {
            config.mqtt_secret_key = encryptedKey;  // Already plaintext
        }
    }

    if (!validateConfig()) {
        LOG_WARN_TAG("ConfigManager", "Config validation failed");
        return false;
    }

    LOG_INFO_TAG("ConfigManager", "Config loaded successfully");
    return true;
}

bool ConfigManagerClass::save() {
    if (!validateConfig()) {
        LOG_WARN_TAG("ConfigManager", "Invalid config, cannot save");
        return false;
    }

    StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;

    doc["wifi_ssid"] = config.wifi_ssid;
    doc["wifi_pass"] = config.wifi_pass;

    doc["llm_host"] = config.llm_host;
    doc["llm_port"] = config.llm_port;
    doc["llm_endpoint"] = config.llm_endpoint;
    doc["llm_model"] = config.llm_model;

    doc["mqtt_host"] = config.mqtt_host;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_topic"] = config.mqtt_topic;
    
    // Encrypt MQTT secret key before saving
    if (config.mqtt_secret_key.length() > 0) {
        String encrypted = SecretManager::encrypt(config.mqtt_secret_key);
        doc["mqtt_secret_key"] = encrypted;
        LOG_DEBUG_TAG("ConfigManager", "MQTT secret key encrypted");
    } else {
        doc["mqtt_secret_key"] = "";
    }

    // Compute integrity hash of the entire config
    String configJson;
    serializeJson(doc, configJson);
    String hash = ConfigIntegrity::computeHash(configJson);
    if (hash.length() > 0) {
        doc["_integrity_hash"] = hash;
        LOG_DEBUG_TAG("ConfigManager", "Config integrity hash added");
    }

    File file = SPIFFS.open(configPath, "w");
    if (!file) {
        LOG_ERROR_TAG("ConfigManager", "Cannot open config file for writing");
        return false;
    }

    serializeJsonPretty(doc, file);
    file.close();

    LOG_INFO_TAG("ConfigManager", "Config saved successfully");
    return true;
}

void portalTask(void* param) {
    ConfigManagerClass* cm = (ConfigManagerClass*)param;

    WiFi.softAP("ArduClaw-Setup");
    LOG_INFO_TAG("ConfigManager", "Setup portal started on 192.168.4.1");

    server.on("/", HTTP_GET, [cm]() {
        String html =
            "<!DOCTYPE html><html><body>"
            "<h1>ArduClaw Setup</h1>"
            "<form method='POST' action='/save'>"
            "WiFi SSID:<input name='wifi_ssid' required><br>"
            "WiFi Pass:<input name='wifi_pass' type='password'><br>"
            "LLM Host:<input name='llm_host' required><br>"
            "LLM Port:<input name='llm_port' type='number' value='8000'><br>"
            "LLM Endpoint:<input name='llm_endpoint' value='/api/v1/completions'><br>"
            "LLM Model:<input name='llm_model' value='gpt-4'><br>"
            "MQTT Host:<input name='mqtt_host'><br>"
            "MQTT Port:<input name='mqtt_port' type='number' value='1883'><br>"
            "MQTT Topic:<input name='mqtt_topic' value='arduclaw'><br>"
            "<button type='submit'>Save</button>"
            "</form></body></html>";

        server.send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, [cm]() {
        cm->config.wifi_ssid = server.arg("wifi_ssid");
        cm->config.wifi_pass = server.arg("wifi_pass");

        cm->config.llm_host  = server.arg("llm_host");
        cm->config.llm_port  = server.arg("llm_port").toInt();
        cm->config.llm_endpoint = server.arg("llm_endpoint");
        cm->config.llm_model = server.arg("llm_model");

        cm->config.mqtt_host = server.arg("mqtt_host");
        cm->config.mqtt_port = server.arg("mqtt_port").toInt();
        cm->config.mqtt_topic = server.arg("mqtt_topic");

        if (cm->save()) {
            server.send(200, "text/plain", "Saved. Rebooting...");
            delay(2000);
            ESP.restart();
        } else {
            server.send(400, "text/plain", "Validation failed");
        }
    });

    server.begin();

    // Keep portal running until stopped
    while (portalRunning) {
        server.handleClient();
        delay(10);
    }

    server.stop();
    WiFi.softAPdisconnect(true);
    vTaskDelete(NULL);
}

void ConfigManagerClass::startPortalAsync() {
    if (portalRunning) {
        LOG_WARN_TAG("ConfigManager", "Portal already running");
        return;
    }

    portalRunning = true;
    xTaskCreatePinnedToCore(
        portalTask,
        "ConfigPortal",
        4096,
        this,
        1,
        &portalTaskHandle,
        0
    );
    LOG_INFO_TAG("ConfigManager", "Portal task started");
}

void ConfigManagerClass::stopPortal() {
    if (portalRunning) {
        portalRunning = false;
        LOG_INFO_TAG("ConfigManager", "Portal stopped");
    }
}