/*
  Voice-first example for Waveshare ESP32-S3-Touch-LCD-1.83
  Flow: STT (microphone) -> LLM -> TTS (speaker)

  Board note:
  Waveshare wiki shows this board has ES7210 (mic ADC) + ES8311 (audio codec).
  This example implements:
    - I2S mic capture
    - simple VAD (voice activity detection)
    - STT HTTP call (local endpoint)
    - Local LLM response
    - TTS HTTP call (local endpoint) + I2S playback

  Local services expected:
  - STT endpoint returns JSON with one of: "text", "transcript", "result"
  - TTS endpoint accepts JSON {"text":"..."} and returns WAV PCM16LE

  Setup:
  1) Fill WiFi + Local LLM endpoint settings.
  2) Run tools/local_voice_server on your PC, then set STT_URL/TTS_URL.
  3) Compile for ESP32-S3 and open Serial Monitor (115200) for logs only.
*/

#include <Arduclaw.h>
#include <providers/AC_LocalLLMProvider.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <Wire.h>
#include "es7210.h"
#include "es8311.h"

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Local LLM endpoint (example: Ollama)
const char* LLM_HOST  = "LLM_IP_ADDRESS";  // e.g. "
const int   LLM_PORT  = 11434;
const char* LLM_PATH  = "/api/generate";
const char* LLM_MODEL = "llama3.2:1b";
const char* ASSISTANT_VARIANT = "English (Indian accent)";  // for fun, adjust response style based on this

// Local STT/TTS service URLs
const char* STT_URL = "http://STT_IP_ADDRESS:8765/stt";
const char* TTS_URL = "http://TTS_IP_ADDRESS:8765/tts";

ArduClaw claw;
AC_LocalLLMProvider localLLM(LLM_HOST, LLM_PORT, LLM_PATH, LLM_MODEL);

static bool g_requestInFlight = false;
static bool g_ttsPlaying = false;
static unsigned long g_lastAttemptMs = 0;
static unsigned long g_resumeListenAtMs = 0;

// ---------------------------
// Waveshare ESP32-S3-Touch-LCD-1.83 audio pin map
// ---------------------------

static const i2s_port_t I2S_PORT = I2S_NUM_1;

// User-provided board wiring
// ES8311 codec: I2C SDA=15, SCL=14, MCLK=16, SCLK=9
// ES7210 ADC: ASDOUT=10, LRCK=45, DSDIN=8, PA_CTRL=46
// Practical I2S mapping:
//   BCLK=SCLK(9), LRCK(45), MIC_DOUT=ASDOUT(10), SPK_DOUT=DSDIN(8)
static const int PIN_I2S_BCLK = 9;
static const int PIN_I2S_LRCK = 45;
static const int PIN_MIC_DOUT = 10;
static const int PIN_SPK_DOUT = 8;
static const int PIN_I2S_MCLK = 16;
static const int PIN_I2C_SCL = 14;
static const int PIN_I2C_SDA = 15;
static const int PIN_PA_CTRL = 46;
static const int PIN_CODEC_CE = 21;

// Audio format
static const int SAMPLE_RATE = 16000;
static const int SAMPLE_BITS = 16;
static const int CHANNELS = 1;
static const int I2S_RX_CHANNELS = 2;  // Read stereo and auto-pick active mic lane

// VAD and capture window
static const int FRAME_SAMPLES = 320;                  // 20ms @ 16kHz
static const int MAX_UTTERANCE_MS = 6000;              // max 6 sec capture
static const int SPEECH_START_THRESHOLD = 300;         // tune per environment
static const int SPEECH_CONTINUE_THRESHOLD = 180;      // tune per environment
static const int SPEECH_END_SILENCE_MS = 700;

static const int MAX_SAMPLES = SAMPLE_RATE * MAX_UTTERANCE_MS / 1000;
static int16_t g_audioSamples[MAX_SAMPLES];
static int g_audioSampleCount = 0;
static bool g_recording = false;
static unsigned long g_lastSpeechMs = 0;
static unsigned long g_lastMicDebugMs = 0;
static int g_i2cSdaInUse = -1;
static int g_i2cSclInUse = -1;
static es8311_handle_t g_es8311 = nullptr;

static bool setI2sStdClock(int sampleRate, i2s_channel_t channels) {
  if (i2s_set_clk(I2S_PORT, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, channels) != ESP_OK) {
    Serial.println("i2s_set_clk failed for " + String(sampleRate));
    return false;
  }
  return true;
}

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
  // Try known/candidate pin mappings across different Waveshare revisions.
  const int candidates[][2] = {
    {PIN_I2C_SDA, PIN_I2C_SCL},  // user-provided first
    {11, 10},                    // common external I2C pads
    {41, 42},
    {42, 41},
    {17, 18},
    {18, 17}
  };

  for (unsigned i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
    int sda = candidates[i][0];
    int scl = candidates[i][1];
    if (probeCodecI2cPins(sda, scl)) {
      g_i2cSdaInUse = sda;
      g_i2cSclInUse = scl;
      Serial.println("Using codec I2C bus SDA=" + String(sda) + " SCL=" + String(scl));
      return;
    }
  }

  Serial.println("No codec I2C bus detected. Audio codec init may fail.");
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
  g_es8311 = es8311_create(0, ES8311_ADDRESS_0);
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

static void writeLe16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void writeLe32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int averageAbs(const int16_t* data, int n) {
  if (n <= 0) return 0;
  uint32_t acc = 0;
  for (int i = 0; i < n; ++i) {
    int v = data[i];
    if (v < 0) v = -v;
    acc += (uint32_t)v;
  }
  return (int)(acc / (uint32_t)n);
}

static uint8_t* buildWavFromPcm16(const int16_t* samples, int sampleCount, int& outSize) {
  const int dataBytes = sampleCount * (SAMPLE_BITS / 8) * CHANNELS;
  const int wavSize = 44 + dataBytes;
  uint8_t* wav = (uint8_t*)malloc((size_t)wavSize);
  if (!wav) {
    outSize = 0;
    return nullptr;
  }

  memcpy(wav + 0, "RIFF", 4);
  writeLe32(wav + 4, (uint32_t)(36 + dataBytes));
  memcpy(wav + 8, "WAVE", 4);
  memcpy(wav + 12, "fmt ", 4);
  writeLe32(wav + 16, 16);
  writeLe16(wav + 20, 1);  // PCM
  writeLe16(wav + 22, CHANNELS);
  writeLe32(wav + 24, SAMPLE_RATE);
  writeLe32(wav + 28, SAMPLE_RATE * CHANNELS * (SAMPLE_BITS / 8));
  writeLe16(wav + 32, CHANNELS * (SAMPLE_BITS / 8));
  writeLe16(wav + 34, SAMPLE_BITS);
  memcpy(wav + 36, "data", 4);
  writeLe32(wav + 40, (uint32_t)dataBytes);
  memcpy(wav + 44, samples, (size_t)dataBytes);

  outSize = wavSize;
  return wav;
}

static bool postWavToStt(const int16_t* samples, int sampleCount, String& transcript) {
  transcript = "";
  if (WiFi.status() != WL_CONNECTED) return false;

  int wavSize = 0;
  uint8_t* wav = buildWavFromPcm16(samples, sampleCount, wavSize);
  if (!wav || wavSize <= 44) return false;

  HTTPClient http;
  http.setTimeout(20000);
  http.begin(STT_URL);
  http.addHeader("Content-Type", "audio/wav");

  int code = http.POST(wav, wavSize);
  free(wav);

  if (code <= 0) {
    Serial.println("STT HTTP error");
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.println("STT: invalid JSON");
    return false;
  }

  if (doc["text"].is<const char*>()) {
    transcript = doc["text"].as<String>();
  } else if (doc["transcript"].is<const char*>()) {
    transcript = doc["transcript"].as<String>();
  } else if (doc["result"].is<const char*>()) {
    transcript = doc["result"].as<String>();
  }

  transcript.trim();
  return transcript.length() > 0;
}

static bool fetchTtsWav(const String& text, uint8_t*& outAudio, int& outLen) {
  outAudio = nullptr;
  outLen = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  StaticJsonDocument<1024> req;
  req["text"] = text;

  String payload;
  serializeJson(req, payload);

  HTTPClient http;
  http.setTimeout(25000);
  http.begin(TTS_URL);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);
  if (code <= 0) {
    Serial.println("TTS HTTP error");
    http.end();
    return false;
  }
  if (code != 200) {
    Serial.println("TTS status: " + String(code));
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int len = http.getSize();
  if (len <= 44 || !stream) {
    http.end();
    return false;
  }

  uint8_t* buf = (uint8_t*)malloc((size_t)len);
  if (!buf) {
    http.end();
    return false;
  }

  int received = 0;
  while (http.connected() && received < len) {
    size_t availBytes = stream->available();
    if (!availBytes) {
      delay(1);
      continue;
    }
    int want = (len - received);
    if ((int)availBytes < want) {
      want = (int)availBytes;
    }
    int n = stream->readBytes(buf + received, (size_t)want);
    if (n <= 0) break;
    received += n;
  }

  http.end();
  if (received <= 44) {
    free(buf);
    return false;
  }

  outAudio = buf;
  outLen = received;
  return true;
}

static bool playWavOverSpeaker(const uint8_t* wav, int wavLen) {
  if (!wav || wavLen <= 44) return false;
  if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
    Serial.println("TTS audio is not WAV");
    return false;
  }

  const uint16_t channels = (uint16_t)wav[22] | ((uint16_t)wav[23] << 8);
  const uint16_t bits = (uint16_t)wav[34] | ((uint16_t)wav[35] << 8);
  const uint32_t wavRate = (uint32_t)wav[24] |
                           ((uint32_t)wav[25] << 8) |
                           ((uint32_t)wav[26] << 16) |
                           ((uint32_t)wav[27] << 24);
  if (bits != 16) {
    Serial.println("TTS WAV must be 16-bit PCM");
    return false;
  }
  if (wavRate < 8000 || wavRate > 48000) {
    Serial.println("Unsupported WAV sample rate: " + String((unsigned long)wavRate));
    return false;
  }
  if (channels != 1 && channels != 2) {
    Serial.println("Unsupported WAV channels: " + String(channels));
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  if (!setI2sStdClock((int)wavRate, I2S_CHANNEL_STEREO)) {
    return false;
  }
  Serial.println("TTS WAV format: " + String((unsigned long)wavRate) + " Hz, ch=" + String(channels));

  const int16_t* pcm = (const int16_t*)(wav + 44);
  const int samplesTotal = (wavLen - 44) / 2;

  if (channels == 2) {
    const uint8_t* raw = wav + 44;
    int bytesLeft = wavLen - 44;
    int offset = 0;
    while (offset < bytesLeft) {
      size_t written = 0;
      int chunk = (bytesLeft - offset > 1024) ? 1024 : (bytesLeft - offset);
      esp_err_t err = i2s_write(I2S_PORT, raw + offset, (size_t)chunk, &written, portMAX_DELAY);
      if (err != ESP_OK) return false;
      offset += (int)written;
    }
    setI2sStdClock(SAMPLE_RATE, I2S_CHANNEL_STEREO);
    return true;
  }

  // Mono input: duplicate to L/R so playback is audible regardless of routed side.
  int idx = 0;
  int16_t stereoBuf[512];  // 256 stereo frames
  while (idx < samplesTotal) {
    int frames = samplesTotal - idx;
    if (frames > 256) frames = 256;
    for (int i = 0; i < frames; ++i) {
      int16_t s = pcm[idx + i];
      stereoBuf[i * 2] = s;
      stereoBuf[i * 2 + 1] = s;
    }
    size_t written = 0;
    esp_err_t err = i2s_write(I2S_PORT, stereoBuf, (size_t)(frames * 2 * sizeof(int16_t)), &written, portMAX_DELAY);
    if (err != ESP_OK) return false;
    idx += (int)(written / (2 * sizeof(int16_t)));
  }
  setI2sStdClock(SAMPLE_RATE, I2S_CHANNEL_STEREO);
  return true;
}

bool initBoardAudio() {
  // NOTE:
  // Many board examples configure ES7210/ES8311 over I2C first.
  // If mic/speaker stay silent, add explicit codec register init here.

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

bool captureSpeechToText(String& transcript) {
  int16_t frameStereo[FRAME_SAMPLES * I2S_RX_CHANNELS];
  int16_t frameMono[FRAME_SAMPLES];
  size_t bytesRead = 0;
  esp_err_t err = i2s_read(I2S_PORT, frameStereo, sizeof(frameStereo), &bytesRead, 10 / portTICK_PERIOD_MS);
  if (err != ESP_OK || bytesRead == 0) {
    return false;
  }

  int stereoSamples = (int)(bytesRead / sizeof(int16_t));
  int stereoFrames = stereoSamples / I2S_RX_CHANNELS;
  if (stereoFrames <= 0) {
    return false;
  }

  int32_t leftAcc = 0;
  int32_t rightAcc = 0;
  for (int i = 0; i < stereoFrames; ++i) {
    int16_t l = frameStereo[i * 2];
    int16_t r = frameStereo[i * 2 + 1];
    leftAcc += (l < 0) ? -l : l;
    rightAcc += (r < 0) ? -r : r;
  }
  bool useRight = rightAcc > leftAcc;
  for (int i = 0; i < stereoFrames; ++i) {
    frameMono[i] = useRight ? frameStereo[i * 2 + 1] : frameStereo[i * 2];
  }

  int samplesRead = stereoFrames;
  int level = averageAbs(frameMono, samplesRead);
  unsigned long now = millis();

  if (now - g_lastMicDebugMs > 1000) {
    g_lastMicDebugMs = now;
    Serial.println("MIC level: " + String(level) +
                   " lane=" + String(useRight ? "R" : "L") +
                   (g_recording ? " (recording)" : ""));
  }

  if (!g_recording) {
    if (level > SPEECH_START_THRESHOLD) {
      g_recording = true;
      g_audioSampleCount = 0;
      g_lastSpeechMs = now;
    } else {
      return false;
    }
  }

  if (g_audioSampleCount + samplesRead > MAX_SAMPLES) {
    samplesRead = MAX_SAMPLES - g_audioSampleCount;
  }
  if (samplesRead > 0) {
    memcpy(g_audioSamples + g_audioSampleCount, frameMono, (size_t)samplesRead * sizeof(int16_t));
    g_audioSampleCount += samplesRead;
  }

  if (level > SPEECH_CONTINUE_THRESHOLD) {
    g_lastSpeechMs = now;
  }

  bool hitMax = g_audioSampleCount >= MAX_SAMPLES;
  bool hitSilence = (now - g_lastSpeechMs) > SPEECH_END_SILENCE_MS;
  if (!hitMax && !hitSilence) {
    return false;
  }

  g_recording = false;
  if (g_audioSampleCount < SAMPLE_RATE / 4) {
    g_audioSampleCount = 0;
    return false;
  }

  bool ok = postWavToStt(g_audioSamples, g_audioSampleCount, transcript);
  g_audioSampleCount = 0;
  if (!ok) {
    Serial.println("STT failed/no text");
  }
  return ok;
}

void speakText(const String& text) {
  g_ttsPlaying = true;
  uint8_t* wav = nullptr;
  int wavLen = 0;
  if (!fetchTtsWav(text, wav, wavLen)) {
    Serial.println("TTS fetch failed");
    g_ttsPlaying = false;
    g_resumeListenAtMs = millis() + 1200;
    return;
  }
  Serial.println("TTS WAV bytes: " + String(wavLen));

  bool played = playWavOverSpeaker(wav, wavLen);
  free(wav);

  if (!played) {
    Serial.println("TTS playback failed");
  } else {
    Serial.println("TTS playback ok");
  }

  g_ttsPlaying = false;
  // Avoid capturing speaker ring-down/echo right after playback.
  g_resumeListenAtMs = millis() + 1200;
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(250);
    Serial.print('.');
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi failed. Check credentials.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== VOICE BUILD MARKER: WAVESHARE_V7 ===");
  Serial.println(String("Build: ") + __DATE__ + " " + __TIME__);
  Serial.println("--- ESP32-S3 Voice Assistant (STT -> LLM -> TTS) ---");

  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!initBoardAudio()) {
    Serial.println("Audio init failed. Verify board pin map and codec init.");
    return;
  }

  localLLM.begin("");
  claw.addProvider(&localLLM);
  claw.begin();

  Serial.println("Voice assistant started. Speak normally near the mics.");
}

void loop() {
  claw.loop();

  if (WiFi.status() != WL_CONNECTED || g_requestInFlight || g_ttsPlaying) {
    return;
  }

  if (millis() < g_resumeListenAtMs) {
    return;
  }

  if (millis() - g_lastAttemptMs < 40) {
    return;
  }
  g_lastAttemptMs = millis();

  String transcript;
  if (!captureSpeechToText(transcript)) {
    return;
  }

  transcript.trim();
  if (transcript.length() == 0) {
    return;
  }

  g_requestInFlight = true;
  Serial.println("Heard: " + transcript);

  String prompt = "Reply only in English (Indian style), in 1-2 concise spoken sentences. "
                  "Do not use Hindi or any non-English script. User said: " + transcript;
  localLLM.sendAsync(
      prompt,
      [](String answer) {
        Serial.println("AI: " + answer);
        speakText(answer);
        g_requestInFlight = false;
      },
      [](String error) {
        g_requestInFlight = false;
        Serial.println("LLM error: " + error);
      });
}
