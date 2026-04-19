#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>
#include <Arduclaw.h>
#include <providers/AC_LocalLLMProvider.h>
#include <driver/i2s.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "es7210.h"
#include "es8311.h"

// Color definitions for Arduino_GFX
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0

// LCD pin configuration
#define LCD_DC 4
#define LCD_CS 5
#define LCD_SCK 6
#define LCD_MOSI 7
#define LCD_RST 38
#define LCD_BL 40
#define LCD_WIDTH 240
#define LCD_HEIGHT 284

// WiFi configuration
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Telegram configuration
const char* TELEGRAM_BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";
const String TELEGRAM_CHAT_ID = "YOUR_TELEGRAM_CHAT_ID";

// Local LLM configuration
const char* LLM_HOST  = "YOUR_LLM_HOSTIP";
const int   LLM_PORT  = 11434;
const char* LLM_PATH  = "/api/generate";
const char* LLM_MODEL = "gemma4:e2b";

// Poll interval (ms)
const unsigned long BOT_POLL_INTERVAL = 2000;

// Audio and ArduClaw
static const i2s_port_t I2S_PORT = I2S_NUM_1;
static const int PIN_I2S_BCLK = 9;
static const int PIN_I2S_LRCK = 45;
static const int PIN_MIC_DOUT = 10;
static const int PIN_SPK_DOUT = 8;
static const int PIN_I2S_MCLK = 16;
static const int PIN_I2C_SCL = 14;
static const int PIN_I2C_SDA = 15;
static const int PIN_PA_CTRL = 46;
static const int PIN_CODEC_CE = 21;
static const int SAMPLE_RATE = 16000;
static const int I2S_RX_CHANNELS = 2;

ArduClaw claw;
AC_LocalLLMProvider localLLM(LLM_HOST, LLM_PORT, LLM_PATH, LLM_MODEL);

// LCD display setup
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST /* RST */, 0 /* rotation */, true /* IPS */,
                                      LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);

WiFiClientSecure securedClient;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, securedClient);

unsigned long lastBotCheck = 0;
String pendingAction = "";
String pendingReplyChatId = "";
String pendingReplyText = "";

// This is the system prompt that gives the LLM context.
// It tells the LLM its role, the available actions, and the required JSON output format.
const char* PROMPT_TEMPLATE =
  "You are an assistant on an ESP32-S3 Touch LCD 1.83 device with a speaker. Your task is to play beeps. "
  "Based on the user's request, you must choose one of the following actions: 'short_beep', 'long_beep'. "
  "Your response MUST be a valid JSON object with an 'action' key. "
  "If the user's request is unclear or does not map to an action, respond with a JSON object containing a 'response' key with a helpful message. "
  "User request: \"%s\""
  "JSON response:";

// ----------------------------------------
// Display functions
// ----------------------------------------

static bool sendTelegramReply(const String& chat_id, const String& text) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected before Telegram send");
    return false;
  }
  if (securedClient.connected()) {
    securedClient.stop();
  }
  securedClient.setInsecure();

  bool sent = bot.sendMessage(chat_id, text, "");
  Serial.println(sent ? "Telegram reply sent" : "Telegram reply failed");
  if (!sent) {
    Serial.println("Failed to send Telegram reply for chat_id: " + chat_id);
  } else {
    Serial.println("Reply text: " + text);
  }
  return sent;
}

void initDisplay() {
  Serial.println("Initializing LCD display...");
  
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
    return;
  }
  
  gfx->setTextSize(2);
  gfx->fillScreen(BLACK);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  
  // Display ready message centered
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds("TelegramBeep Ready!", 0, 0, &x1, &y1, &w, &h);
  int x = (LCD_WIDTH - w) / 2;
  int y = (LCD_HEIGHT - h) / 2;
  gfx->setCursor(x, y);
  gfx->setTextColor(RED);
  gfx->println("TelegramBeep Ready!");
  delay(1000);
}

void displayQuery(const String& query) {
  gfx->setTextSize(2);
  gfx->fillScreen(BLACK);
  
  // Display "Request:" header centered
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds("Request:", 0, 0, &x1, &y1, &w, &h);
  int x = (LCD_WIDTH - w) / 2;
  gfx->setCursor(x, 10);
  gfx->setTextColor(CYAN);
  gfx->println("Request:");
  
  // Display the query text
  gfx->setCursor(20, 40);
  gfx->setTextColor(WHITE);
  
  // Simple word wrapping
  int cursorX = 20, cursorY = 40;
  String word = "";
  for (int i = 0; i < query.length(); i++) {
    if (query[i] == ' ' || i == query.length() - 1) {
      if (i == query.length() - 1) word += query[i];
      
      // Check if word fits on current line
      int wordWidth = word.length() * 12;
      if (query[i] == ' ') wordWidth += 12; // add space
      if (cursorX + wordWidth > LCD_WIDTH - 20) {
        cursorY += 20;
        cursorX = 20;
        gfx->setCursor(cursorX, cursorY);
      }
      
      gfx->print(word);
      cursorX += word.length() * 12;
      if (query[i] == ' ') {
        gfx->print(" ");
        cursorX += 12;
      }
      word = "";
    } else {
      word += query[i];
    }
  }
}

void displayAction(const String& action) {
  gfx->setTextSize(2);
  
  // Display action at bottom of screen
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds("Action:", 0, 0, &x1, &y1, &w, &h);
  int x = (LCD_WIDTH - w) / 2;
  gfx->setCursor(x, LCD_HEIGHT - 80);
  gfx->setTextColor(GREEN);
  gfx->println("Action:");
  
  gfx->getTextBounds(action, 0, 0, &x1, &y1, &w, &h);
  x = (LCD_WIDTH - w) / 2;
  gfx->setCursor(x, LCD_HEIGHT - 50);
  gfx->setTextColor(YELLOW);
  gfx->println(action);
}

// ----------------------------------------

static bool i2cDeviceExists(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static int scanI2cDevicesOnCurrentBus() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.println("I2C device: 0x" + String(addr, HEX));
      found++;
    }
  }
  return found;
}

static bool probeCodecI2cPins(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl);
  delay(20);
  Serial.println("Probing I2C SDA=" + String(sda) + " SCL=" + String(scl));
  int found = scanI2cDevicesOnCurrentBus();
  if (!found) {
    Serial.println("I2C scan: no devices found");
  }
  bool hasES8311 = i2cDeviceExists(0x18);
  bool hasES7210 = i2cDeviceExists(0x40);
  Serial.println(String("ES8311(0x18): ") + (hasES8311 ? "OK" : "MISSING"));
  Serial.println(String("ES7210(0x40): ") + (hasES7210 ? "OK" : "MISSING"));
  return hasES8311 || hasES7210;
}

static void initI2cForCodecs() {
  const int candidates[][2] = {
    {PIN_I2C_SDA, PIN_I2C_SCL},
    {11, 10},
    {41, 42},
    {42, 41},
    {17, 18},
    {18, 17}
  };

  for (unsigned i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
    int sda = candidates[i][0];
    int scl = candidates[i][1];
    if (probeCodecI2cPins(sda, scl)) {
      Serial.println("Using codec I2C bus SDA=" + String(sda) + " SCL=" + String(scl));
      return;
    }
  }

  Serial.println("No codec I2C bus detected. Audio codec init may fail.");
}

static bool setI2sStdClock(int sampleRate, i2s_channel_t channels) {
  if (i2s_set_clk(I2S_PORT, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, channels) != ESP_OK) {
    Serial.println("i2s_set_clk failed for " + String(sampleRate));
    return false;
  }
  return true;
}

static void playBeep(int durationMs) {
  const int rate = SAMPLE_RATE;
  const int totalFrames = (rate * durationMs) / 1000;
  const int halfPeriod = rate / (1200 * 2);
  int16_t buf[512];

  i2s_zero_dma_buffer(I2S_PORT);
  setI2sStdClock(rate, I2S_CHANNEL_STEREO);

  int frame = 0;
  while (frame < totalFrames) {
    int framesThis = totalFrames - frame;
    if (framesThis > 256) framesThis = 256;
    for (int i = 0; i < framesThis; ++i) {
      int phase = ((frame + i) / halfPeriod) & 1;
      int16_t s = phase ? 2200 : -2200;
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    size_t written = 0;
    i2s_write(I2S_PORT, buf, (size_t)(framesThis * 2 * sizeof(int16_t)), &written, portMAX_DELAY);
    frame += (int)(written / (2 * sizeof(int16_t)));
    yield();
  }
}

static bool initEs7210Adc() {
  audio_hal_codec_config_t cfg = {
    .adc_input = AUDIO_HAL_ADC_INPUT_ALL,
    .codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE,
    .i2s_iface = {
      .mode = AUDIO_HAL_MODE_SLAVE,
      .fmt = AUDIO_HAL_I2S_NORMAL,
      .samples = AUDIO_HAL_16K_SAMPLES,
      .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
    },
  };

  if (es7210_adc_init(&Wire, &cfg) != ESP_OK) {
    Serial.println("ES7210 init failed");
    return false;
  }
  if (es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface) != ESP_OK) {
    Serial.println("ES7210 I2S config failed");
    return false;
  }

  es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2), (es7210_gain_value_t)GAIN_24DB);
  es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4), (es7210_gain_value_t)GAIN_24DB);
  es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);

  Serial.println("ES7210 initialized.");
  return true;
}

static bool initEs8311Codec() {
  es8311_handle_t g_es8311 = es8311_create(0, ES8311_ADDRESS_0);
  if (!g_es8311) {
    Serial.println("ES8311 create failed");
    return false;
  }

  es8311_clock_config_t clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = SAMPLE_RATE * 256,
    .sample_frequency = SAMPLE_RATE
  };

  if (es8311_init(g_es8311, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("ES8311 init failed");
    return false;
  }
  if (es8311_sample_frequency_config(g_es8311, clk.mclk_frequency, clk.sample_frequency) != ESP_OK) {
    Serial.println("ES8311 fs config failed");
    return false;
  }
  es8311_microphone_config(g_es8311, false);
  es8311_microphone_gain_set(g_es8311, ES8311_MIC_GAIN_18DB);
  es8311_voice_volume_set(g_es8311, 85, nullptr);
  es8311_voice_mute(g_es8311, false);
  Serial.println("ES8311 initialized.");
  return true;
}

static bool initBoardAudio() {
  initI2cForCodecs();

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ALL_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 256 * SAMPLE_RATE;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRCK;
  pins.data_out_num = PIN_SPK_DOUT;
  pins.data_in_num = PIN_MIC_DOUT;
  pins.mck_io_num = PIN_I2S_MCLK;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("I2S install failed");
    return false;
  }
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    Serial.println("I2S pin setup failed");
    return false;
  }
  if (i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
    Serial.println("I2S clock setup failed");
    return false;
  }

  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, HIGH);
  Serial.println("PA_CTRL enabled on GPIO46.");

  pinMode(PIN_CODEC_CE, OUTPUT);
  digitalWrite(PIN_CODEC_CE, HIGH);
  Serial.println("Codec_CE enabled on GPIO21.");

  if (!initEs7210Adc()) {
    return false;
  }
  if (!initEs8311Codec()) {
    return false;
  }

  Serial.println("I2S mic/speaker configured (shared clock + MCLK).");
  return true;
}

// ----------------------------------------

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  securedClient.setInsecure(); // skip certificate validation

  // Initialize display
  initDisplay();

  // Initialize local LLM provider and ArduClaw
  claw.begin();
  localLLM.begin("");
  claw.addProvider(&localLLM);

  if (!initBoardAudio()) {
    Serial.println("Warning: audio init failed. Beeps may not work.");
  }

  // Register expected actions for validation
  claw.registerAction("short_beep", {}, PERMISSION_LOW);
  claw.registerAction("long_beep", {}, PERMISSION_LOW);

}

void handleAction(const String& action) {
  Serial.println("handleAction: " + action);
  if (action == "short_beep") {
    playBeep(120);
    Serial.println("Short beep played");
    delay(10);
    displayAction("short_beep executed");
  } else if (action == "long_beep") {
    playBeep(300);
    Serial.println("Long beep played");
    delay(10);
    displayAction("long_beep executed");
  }
}

void loop() {
  claw.loop();

  if (pendingReplyChatId.length() > 0) {
    Serial.println("Sending pending Telegram reply");
    if (sendTelegramReply(pendingReplyChatId, pendingReplyText)) {
      pendingReplyChatId = "";
      pendingReplyText = "";
    }
  }

  if (pendingAction.length() > 0) {
    Serial.println("Executing pending action: " + pendingAction);
    handleAction(pendingAction);
    pendingAction = "";
  }

  if (millis() - lastBotCheck > BOT_POLL_INTERVAL) {
    int numNew = bot.getUpdates(bot.last_message_received + 1);
    for (int i = 0; i < numNew; i++) {
      String text = bot.messages[i].text;
      String chat_id = bot.messages[i].chat_id;
      if (text.length() > 0) {
        Serial.println("User: " + text);
        displayQuery(text);

        // Format the full prompt with context
        char fullPrompt[512];
        snprintf(fullPrompt, sizeof(fullPrompt), PROMPT_TEMPLATE, text.c_str());

        // Send the full, contextual prompt to the LLM
        claw.ask(fullPrompt, [chat_id](JsonDocument& doc) {
          String replyText = "";
          if (doc.containsKey("action")) {
            String action = doc["action"].as<String>();
            replyText = "Action executed: " + action;
            pendingAction = action;
            pendingReplyChatId = chat_id;
            pendingReplyText = replyText;
            Serial.println("Pending action queued: " + action);
            Serial.println("Pending reply queued: " + replyText);
          } else if (doc.containsKey("response")) {
            replyText = doc["response"].as<String>();
            pendingReplyChatId = chat_id;
            pendingReplyText = replyText;
            Serial.println("Pending reply queued: " + replyText);
          } else {
            String out;
            serializeJson(doc, out);
            replyText = out;
            pendingReplyChatId = chat_id;
            pendingReplyText = replyText;
            Serial.println("Pending reply queued: " + replyText);
          }
        });
      }
    }
    lastBotCheck = millis();
  }

  delay(10);
}