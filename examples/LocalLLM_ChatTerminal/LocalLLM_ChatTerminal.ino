/*
  STEP 1: Connect ESP32 to WiFi
  STEP 2: Configure Local LLM settings (Host IP, Port, Model)
  STEP 3: Open Serial Monitor (115200 baud)
*/
#include <Arduclaw.h>
#include <providers/AC_LocalLLMProvider.h>
#include <core/WatchdogHelper.h>
#include <WiFi.h>

// --- Beginner WiFi: paste your credentials here ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// --- Local LLM Configuration ---
// Ensure your PC/Server is on the same network and firewall allows connections.
// Example settings for Ollama:
const char* LLM_HOST  = "YOUR_LLM_HOST_IP";  // IP of your computer running LLM
const int   LLM_PORT  = 11434;            // Default port (e.g. 11434 for Ollama)
const char* LLM_PATH  = "/api/generate"; // Endpoint (Ollama uses /api/generate)
const char* LLM_MODEL = "YOUR_LLM_MODEL_NAME";   // Model name to use

ArduClaw claw;
AC_LocalLLMProvider localLLM(LLM_HOST, LLM_PORT, LLM_PATH, LLM_MODEL);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("--- ArduClaw Local LLM Chat Terminal ---");

  WatchdogHelper::begin();

  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0; // Max 20 attempts * 500ms = 10 seconds
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    WatchdogHelper::delayWithFeed(500);
    Serial.print('.');
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi failed. Please check credentials.");
    return;
  }

  claw.begin();
  localLLM.begin("");
  claw.addProvider(&localLLM);

  Serial.println("Local LLM Chat ready. Type a message and press ENTER:");
}

void loop() {
  claw.loop();

  if (Serial.available()) {
    String text = Serial.readStringUntil('\n');
    text.trim();
    if (text.length() == 0) return;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Not connected to WiFi - responses may fail.");
    }

    Serial.println("Sending to Local LLM (" + String(LLM_HOST) + ")...");

    // Use provider directly for chat replies (no action-schema validation).
    localLLM.sendAsync(
      text,
      [](String answer) {
        Serial.println("Assistant: " + answer);
      },
      [](String error) {
        Serial.println("Error: " + error);
      }
    );
  }
}
