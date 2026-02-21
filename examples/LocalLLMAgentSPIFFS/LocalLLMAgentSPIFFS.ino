#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include "Arduclaw.h"
#include <providers/AC_LocalLLMProvider.h>

// ============================
// WiFi Credentials
// ============================

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ============================
// Hardware
// ============================

#define RELAY_PIN 2

// ============================
// Config Structure
// ============================

struct LLMConfig {
    String host;
    int port;
    String endpoint;
    String model;
    int timeout;
};

LLMConfig llmConfig;

// ============================
// Load Config From SPIFFS
// ============================

bool loadLLMConfig() {

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return false;
    }

    File file = SPIFFS.open("/llm_config.json", "r");
    if (!file) {
        Serial.println("Config file not found");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("JSON parse failed");
        return false;
    }

    llmConfig.host     = doc["host"].as<String>();
    llmConfig.port     = doc["port"];
    llmConfig.endpoint = doc["endpoint"].as<String>();
    llmConfig.model    = doc["model"].as<String>();
    llmConfig.timeout  = doc["timeout"] | 8000;

    return true;
}

// ============================
// Globals
// ============================

ArduClaw* claw;
AC_LocalLLMProvider* localLLM;

// ============================
// Serial Channel
// ============================

class SerialChannel : public BaseChannel {

private:
    String buffer;

public:
    void begin() override {
        Serial.println("Local LLM Agent (SPIFFS Config)");
    }

    void loop() override {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') buffer.trim();
            else buffer += c;
        }
    }

    bool available() override {
        return buffer.length() > 0;
    }

    String readMessage() override {
        String msg = buffer;
        buffer = "";
        return msg;
    }

    void sendMessage(const String& msg) override {
        Serial.println("Response: " + msg);
    }
};

SerialChannel serialChannel;

// ============================
// Setup
// ============================

void setup() {

    Serial.begin(115200);
    delay(1000);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    Serial.println("Connecting WiFi...");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi Connected");

    if (!loadLLMConfig()) {
        Serial.println("Failed to load LLM config. Halting.");
        while (true) delay(1000);
    }

    Serial.println("LLM Config Loaded:");
    Serial.println("Host: " + llmConfig.host);
    Serial.println("Model: " + llmConfig.model);

    // Create provider dynamically using config
    localLLM = new AC_LocalLLMProvider(
        llmConfig.host.c_str(),
        llmConfig.port,
        llmConfig.endpoint.c_str(),
        llmConfig.model.c_str()
    );

    localLLM->setTimeout(llmConfig.timeout);

    claw = new ArduClaw();

    claw->addProvider(localLLM);
    claw->addChannel(&serialChannel);

    claw->registerAction("relay_on", {}, PERMISSION_LOW);
    claw->registerAction("relay_off", {}, PERMISSION_LOW);

    claw->onAction([](JsonDocument& doc) {

        String action = doc["action"];

        if (action == "relay_on") {
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println("Relay ON");
        }
        else if (action == "relay_off") {
            digitalWrite(RELAY_PIN, LOW);
            Serial.println("Relay OFF");
        }
    });

    claw->begin();
}

// ============================
// Loop
// ============================

void loop() {
    claw->loop();
    delay(10);
}