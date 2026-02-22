#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Arduclaw.h>
#include <providers/AC_LocalLLMProvider.h>
#include "src/ConfigManager.h" // Use the library's config manager

// ============================
// Hardware
// ============================
#define RELAY_PIN 2

// ============================
// Globals
// ============================
ArduClaw claw;
AC_LocalLLMProvider* localLLM = nullptr; // Will be created after loading config

// ============================
// Serial Channel
// ============================
// A more robust implementation of a serial channel.
class SerialChannel : public BaseChannel {
private:
    String _message;
    bool _available = false;
public:
    void begin() override {
        Serial.println("Local LLM Agent (SPIFFS Config)");
        Serial.println("Type a message and press Enter to send to the LLM.");
    }

    void loop() override {
        if (Serial.available() && !_available) {
            _message = Serial.readStringUntil('\n');
            _message.trim();
            if (_message.length() > 0) {
                _available = true;
            }
        }
    }

    bool available() override {
        return _available;
    }

    String readMessage() override {
        _available = false;
        return _message;
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

    // Use the library's ConfigManager to load settings from SPIFFS
    ConfigManager.begin();
    if (!ConfigManager.load()) {
        Serial.println("Failed to load config.json from SPIFFS. Halting.");
        Serial.println("Please upload a config file with WiFi and LLM settings.");
        while (true) delay(1000);
    }

    WiFi.begin(ConfigManager.config.wifi_ssid.c_str(), ConfigManager.config.wifi_pass.c_str());

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected");

    Serial.println("LLM Config Loaded:");
    Serial.println("Host: " + ConfigManager.config.llm_host);
    Serial.println("Model: " + ConfigManager.config.llm_model);

    // Create provider using the loaded config
    localLLM = new AC_LocalLLMProvider(
        ConfigManager.config.llm_host,
        ConfigManager.config.llm_port,
        ConfigManager.config.llm_endpoint,
        ConfigManager.config.llm_model
    );

    claw.addProvider(localLLM);
    claw.addChannel(&serialChannel);

    claw.registerAction("relay_on", {}, PERMISSION_LOW);
    claw.registerAction("relay_off", {}, PERMISSION_LOW);
    claw.onAction([](JsonDocument& doc) {
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

    claw.begin();
}

// ============================
// Loop
// ============================
void loop() {
    claw.loop();
    delay(10);
}