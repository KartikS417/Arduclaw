#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>
#include <Arduclaw.h>
#include <providers/AC_SarvamProvider.h>
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASS";

const char* TELEGRAM_BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";
const String TELEGRAM_CHAT_ID = "YOUR_CHAT_ID";

const char* API_KEY = "YOUR_API_KEY";

// Poll interval (ms)
const unsigned long BOT_POLL_INTERVAL = 2000;

// LED and ArduClaw
#define LED_PIN LED_BUILTIN

ArduClaw claw;
AC_SarvamProvider provider;

WiFiClientSecure securedClient;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, securedClient);

unsigned long lastBotCheck = 0;

// This is the system prompt that gives the LLM context.
// It tells the LLM its role, the available actions, and the required JSON output format.
const char* PROMPT_TEMPLATE =
  "You are an assistant on an ESP32 microcontroller. Your task is to control an LED. "
  "Based on the user's request, you must choose one of the following actions: 'led_on', 'led_off'. "
  "Your response MUST be a valid JSON object with an 'action' key. "
  "If the user's request is unclear or does not map to an action, respond with a JSON object containing a 'response' key with a helpful message. "
  "User request: \"%s\""
  "JSON response:";

// ----------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  securedClient.setInsecure(); // skip certificate validation

  // Initialize provider and ArduClaw
  provider.begin(String(API_KEY));
  claw.addProvider(&provider);

  // Register expected actions for validation
  claw.registerAction("led_on", {}, PERMISSION_LOW);
  claw.registerAction("led_off", {}, PERMISSION_LOW);

  claw.begin();
}

void handleAction(const String& action) {
  if (action == "led_on") {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED turned ON");
  } else if (action == "led_off") {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED turned OFF");
  }
}

void loop() {
  claw.loop();

  if (millis() - lastBotCheck > BOT_POLL_INTERVAL) {
    int numNew = bot.getUpdates(bot.last_message_received + 1);
    for (int i = 0; i < numNew; i++) {
      String text = bot.messages[i].text;
      String chat_id = bot.messages[i].chat_id;
      if (text.length() > 0) {
        Serial.println("User: " + text);

        // Format the full prompt with context
        char fullPrompt[512];
        snprintf(fullPrompt, sizeof(fullPrompt), PROMPT_TEMPLATE, text.c_str());

        // Send the full, contextual prompt to the LLM
        claw.ask(fullPrompt, [chat_id](JsonDocument& doc) {
          if (doc.containsKey("action")) {
            String action = doc["action"].as<String>();
            handleAction(action);
            bot.sendMessage(chat_id, "Action executed: " + action, "");
          } else if (doc.containsKey("response")) {
            String response = doc["response"].as<String>();
            bot.sendMessage(chat_id, response, "");
          } else {
            String out;
            serializeJson(doc, out);
            bot.sendMessage(chat_id, out, "");
          }
        });
      }
    }
    lastBotCheck = millis();
  }

  delay(10);
}