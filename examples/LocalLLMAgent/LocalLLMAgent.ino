#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include <ArduClaw.h>
#include <ProviderManager.h>
#include <LocalLLMProvider.h>

ArduClaw claw;
ProviderManager providerManager;
LocalLLMProvider localProvider;

// ===== USER CONFIG =====
const char* ssid     = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

// SPIFFS config file
#define CONFIG_FILE "/llm_config.json"

void loadLocalLLMConfig()
{
    if (!SPIFFS.exists(CONFIG_FILE)) {
        Serial.println("No config file found.");
        return;
    }

    File f = SPIFFS.open(CONFIG_FILE, "r");
    if (!f) {
        Serial.println("Failed to open config file");
        return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("Invalid JSON config");
        return;
    }

    const char* host = doc["host"] | "192.168.1.10";
    uint16_t port    = doc["port"] | 8000;
    const char* path = doc["path"] | "/v1/chat/completions";

    localProvider.setEndpoint(host, port, path);

    Serial.println("Local LLM config loaded.");
}

void connectWiFi()
{
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected.");
}

void setup()
{
    Serial.begin(115200);

    SPIFFS.begin(true);

    connectWiFi();
    loadLocalLLMConfig();

    // Register provider
    providerManager.registerProvider("local", &localProvider);
    providerManager.setActiveProvider("local");

    claw.setProviderManager(&providerManager);
    claw.begin();

    Serial.println("Local LLM Example Ready.");
}

void loop()
{
    claw.loop();

    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        claw.ask(input);
    }
}