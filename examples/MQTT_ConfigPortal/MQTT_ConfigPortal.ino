#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "Arduclaw.h"
#include <core/ProviderManager.h>
#include <providers/AC_LocalLLMProvider.h>

ArduClaw claw;
ProviderManager providerManager;
AC_LocalLLMProvider localProvider;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ===== USER CONFIG =====
const char* ssid       = "YOUR_WIFI";
const char* password   = "YOUR_PASSWORD";
const char* mqttHost   = "192.168.1.50";
const uint16_t mqttPort = 1883;

#define CONFIG_FILE "/llm_config.json"
#define CONFIG_TOPIC "arduclaw/config"

void saveConfigToSPIFFS(const JsonDocument& doc)
{
    File f = SPIFFS.open(CONFIG_FILE, "w");
    if (!f) {
        Serial.println("Failed to save config");
        return;
    }

    serializeJson(doc, f);
    f.close();
}

void applyConfig(const JsonDocument& doc)
{
    const char* host = doc["host"] | "192.168.1.10";
    uint16_t port    = doc["port"] | 8000;
    const char* path = doc["path"] | "/v1/chat/completions";

    localProvider.setEndpoint(host, port, path);

    providerManager.setActiveProvider("local");

    Serial.println("New LLM config applied.");
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    StaticJsonDocument<512> doc;

    DeserializationError err = deserializeJson(doc, payload, length);

    if (err) {
        Serial.println("Invalid MQTT JSON");
        return;
    }

    applyConfig(doc);
    saveConfigToSPIFFS(doc);
}

void connectWiFi()
{
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
}

void connectMQTT()
{
    while (!mqttClient.connected()) {
        if (mqttClient.connect("ArduClawClient")) {
            mqttClient.subscribe(CONFIG_TOPIC);
        } else {
            delay(2000);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    SPIFFS.begin(true);

    connectWiFi();

    mqttClient.setServer(mqttHost, mqttPort);
    mqttClient.setCallback(mqttCallback);

    providerManager.registerProvider("local", &localProvider);
    claw.setProviderManager(&providerManager);
    claw.begin();

    connectMQTT();

    Serial.println("MQTT Config Example Ready.");
}

void loop()
{
    if (!mqttClient.connected()) {
        connectMQTT();
    }

    mqttClient.loop();
    claw.loop();
}