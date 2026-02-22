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

const char* SARVAM_API_KEY = "YOUR_SARVAM_API_KEY";

// Poll interval (ms)
const unsigned long BOT_POLL_INTERVAL = 2000;

// LED and ArduClaw
#define LED_PIN LED_BUILTIN

ArduClaw claw;
AC_SarvamProvider sarvam;

WiFiClientSecure securedClient;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, securedClient);

unsigned long lastBotCheck = 0;

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

  // Initialize Sarvam provider and ArduClaw
  sarvam.begin(String(SARVAM_API_KEY));
  claw.addProvider(&sarvam);

  // Register expected actions for validation
  claw.registerAction("led_on", {}, PERMISSION_LOW);
  claw.registerAction("led_off", {}, PERMISSION_LOW);

  claw.begin();
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

        claw.ask(text, [chat_id](JsonDocument& doc) {
          if (doc.containsKey("action")) {
            String action = doc["action"].as<String>();
            if (action == "led_on") {
              digitalWrite(LED_PIN, HIGH);
              bot.sendMessage(chat_id, "LED turned ON", "");
            } else if (action == "led_off") {
              digitalWrite(LED_PIN, LOW);
              bot.sendMessage(chat_id, "LED turned OFF", "");
            } else {
              String out;
              serializeJson(doc, out);
              bot.sendMessage(chat_id, out, "");
            }
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