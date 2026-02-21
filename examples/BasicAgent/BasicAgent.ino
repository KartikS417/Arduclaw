#include <WiFi.h>
#include <ArduClaw.h>
#include <providers/AC_OpenAIProvider.h>

// ============================
// WiFi Credentials
// ============================

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ============================
// OpenAI API Key
// ============================

String openaiKey = "YOUR_OPENAI_API_KEY";

// ============================
// Hardware
// ============================

#define RELAY_PIN 2

// ============================
// Global Objects
// ============================

ArduClaw claw;
AC_OpenAIProvider openai(openaiKey);

// ============================
// Serial Channel Implementation
// ============================

class SerialChannel : public BaseChannel {
private:
    String buffer;

public:
    void begin() override {
        Serial.println("Serial Channel Ready.");
        Serial.println("Type a command and press ENTER.");
    }

    void loop() override {

        while (Serial.available()) {

            char c = Serial.read();

            if (c == '\n') {
                buffer.trim();
            } else {
                buffer += c;
            }
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
        Serial.println("Channel Response: " + msg);
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

    Serial.println("Connecting to WiFi...");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // ============================
    // Register Provider
    // ============================

    claw.addProvider(&openai);

    // ============================
    // Register Channel
    // ============================

    claw.addChannel(&serialChannel);

    // ============================
    // Register Actions
    // ============================

    claw.registerAction(
        "relay_on",
        {},                 // no required fields
        PERMISSION_LOW
    );

    claw.registerAction(
        "relay_off",
        {},
        PERMISSION_LOW
    );

    // ============================
    // Action Execution Hook
    // ============================

    claw.onAction([](JsonDocument& doc) {

        String action = doc["action"];

        if (action == "relay_on") {
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println("Relay turned ON");
        }
        else if (action == "relay_off") {
            digitalWrite(RELAY_PIN, LOW);
            Serial.println("Relay turned OFF");
        }
        else {
            Serial.println("Unknown action received.");
        }
    });

    // ============================
    // Begin ArduClaw
    // ============================

    claw.begin();

    Serial.println("\nArduClaw Ready.");
    Serial.println("Try typing:");
    Serial.println("  turn on relay");
    Serial.println("  turn off relay");
}

// ============================
// Loop
// ============================

void loop() {

    claw.loop();

    delay(10);
}
