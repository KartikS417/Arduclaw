/*
  Telegram Chat -> Arduclaw example (ESP32)

  - Install the UniversalTelegramBot library and dependencies.
  - Fill in `WIFI_SSID`, `WIFI_PASS`, and `BOT_TOKEN`.
  - This example polls Telegram for new messages and sends each message
    to the configured LLM provider via `ArduClaw::ask()`. Replies are
    returned to the same Telegram chat.

  Note: For simplicity this example uses a polling interval and
  `WiFiClientSecure.setInsecure()` for TLS (not recommended for
  production). Replace with CA pinning when deploying.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Arduclaw.h>
#include <providers/AC_SarvamProvider.h>

// --- User config (paste credentials/token) ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* BOT_TOKEN  = "YOUR_TELEGRAM_BOT_TOKEN"; // e.g. 123456:ABC-DEF

// Poll interval (ms)
const unsigned long BOT_POLL_INTERVAL = 2000;

// LED pin for simple remote control (change if needed)
#ifndef LED_PIN
#define LED_PIN 2
#endif

WiFiClientSecure securedClient;
UniversalTelegramBot bot(BOT_TOKEN, securedClient);

ArduClaw claw;
AC_SarvamProvider sarvam;
const char* SARVAM_API_KEY = "YOUR_SARVAM_API_KEY";

unsigned long lastBotCheck = 0;

void connectWiFi() {
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
    Serial.println("\nWiFi connect timed out");
  }
}

void handleMessage(int chat_id, String &text) {
  Serial.println("Received msg: " + text);

  // Use Sarvam to detect intent (ON/OFF/NONE) in user's language
  String detectPrompt = "You are an intent classifier.\n";
  detectPrompt += "Detect if the following user message (which may be in any Indian language) requests turning a device ON or OFF.\n";
  detectPrompt += "Reply exactly with one word: ON, OFF, or NONE.\n";
  detectPrompt += "Message: \"" + text + "\"\n";

  String intentResp = sarvam.generate(detectPrompt);
  intentResp.trim();
  intentResp.toUpperCase();

  if (intentResp.indexOf("ON") != -1) {
    digitalWrite(LED_PIN, HIGH);
    bot.sendMessage(String(chat_id), "LED turned ON", "");
    return;
  }
  if (intentResp.indexOf("OFF") != -1) {
    digitalWrite(LED_PIN, LOW);
    bot.sendMessage(String(chat_id), "LED turned OFF", "");
    return;
  }

  // If no device intent, forward to Sarvam for a conversational reply in same language
  String convoPrompt = "You are a helpful assistant. Reply in the same language as the user.\nUser: \"" + text + "\"\nAssistant:";
  String replyText = sarvam.generate(convoPrompt);
  bot.sendMessage(String(chat_id), replyText, "");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  connectWiFi();

  // Insecure TLS for quick start - replace with CA cert in production
  securedClient.setInsecure();

  // Init Sarvam provider and ArduClaw
  if (!sarvam.begin(String(SARVAM_API_KEY))) {
    Serial.println("Warning: Sarvam provider failed to initialize");
  }
  // We call Sarvam directly for intent detection/replies in this example
  claw.begin();

  // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("Telegram -> Arduclaw bridge ready.");
}

void loop() {
  claw.loop();

  // Poll Telegram periodically
  if (millis() - lastBotCheck > BOT_POLL_INTERVAL) {
    int numNew = bot.getUpdates(bot.last_message_received + 1);
    for (int i = 0; i < bot.messages.size(); i++) {
      String text = bot.messages[i].text;
      int chat_id = bot.messages[i].chat_id;
      if (text.length() > 0) {
        handleMessage(chat_id, text);
      }
    }
    lastBotCheck = millis();
  }
}
