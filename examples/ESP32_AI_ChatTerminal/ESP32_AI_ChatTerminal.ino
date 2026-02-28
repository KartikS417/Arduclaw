/*
  STEP 1: Connect ESP32 to WiFi
  STEP 2: Replace YOUR_API_KEY
  STEP 3: Open Serial Monitor (115200 baud)
*/

#include <Arduclaw.h>
#include <providers/AC_OpenAIProvider.h>
#include <WiFi.h>

// --- Beginner WiFi: paste your credentials here ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* API_KEY = "YOUR_API_KEY";

ArduClaw claw;
AC_OpenAIProvider ai;

void setup() {
  Serial.begin(115200);
  delay(500);

  // Simple blocking WiFi connect with short timeout (keeps example easy)
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print('.');
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi not connected — continue without network");
  }
  if (WiFi.status() != WL_CONNECTED) {
  Serial.println("WiFi failed. Please check credentials.");
  return;
}

  // Initialize provider & library
  ai.begin(API_KEY);
  claw.addProvider(&ai);
  claw.begin();

  Serial.println("FriendlyChat ready. Type a message and press ENTER:");
}

void loop() {
  claw.loop();

  if (Serial.available()) {
    String text = Serial.readStringUntil('\n');
    text.trim();
    if (text.length() == 0) return;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Not connected to WiFi — responses may fail.");
    }

    Serial.println("Sending to LLM...");

    claw.ask(text, [](JsonDocument& resp) {
      if (resp.containsKey("response")) {
        String answer = resp["response"].as<String>();
        Serial.println("Assistant: " + answer);
      } else {
        String out;
        serializeJson(resp, out);
        Serial.println("Assistant (raw): " + out);
      }
    });
  }
}