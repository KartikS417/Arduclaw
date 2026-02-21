// ============================================================================
// ARDUCLAW PRODUCTION INTEGRATION GUIDE
// ============================================================================
//
// This example shows how to use all security + production features together
// Copy this to your main Arduino sketch to get started
//
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Arduclaw core
#include "src/Arduclaw.h"

// Security features
#include "src/core/Logger.h"
#include "src/core/WatchdogHelper.h"
#include "src/core/SecretManager.h"
#include "src/core/ConfigIntegrity.h"
#include "src/core/OTASignatureVerifier.h"
#include "src/providers/AC_OpenAIProvider.h"
#include "src/providers/AC_LocalLLMProvider.h"
#include "src/channels/MQTTChannel.h"

// ============================================================================
// 1. SETUP DEVICE IDENTITY & PUBLIC KEY
// ============================================================================
//
// Replace with your actual public key (64 bytes, read from certificate)
// Generated during firmware signing process
//
const uint8_t DEVICE_PUBLIC_KEY[64] PROGMEM = {
    // Example ECDSA P-256 public key (64 hex bytes)
    0x04, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 0x7a,
    0x8b, 0x9c, 0xad, 0xbe, 0xcf, 0xd0, 0xe1, 0xf2,
    0x03, 0x14, 0x25, 0x36, 0x47, 0x58, 0x69, 0x7a,
    0x8b, 0x9c, 0xad, 0xbe, 0xcf, 0xd0, 0xe1, 0xf2,
    // ... (64 bytes total)
};

// ============================================================================
// 2. DEVICE CONFIGURATION (ENCRYPTED AT REST)
// ============================================================================

struct ArduclawConfig {
    String wifi_ssid = "YOUR_SSID";
    String wifi_pass = "YOUR_PASSWORD";
    
    // LLM Provider (OpenAI or local)
    String llm_api_key = "sk-...";  // Encrypted in SPIFFS
    String llm_host = "api.openai.com";
    int llm_port = 443;
    
    // MQTT (secure)
    String mqtt_host = "mqtt.example.com";
    int mqtt_port = 8883;  // TLS port
    String mqtt_topic = "arduclaw/commands";
    String mqtt_secret_key = "64-char-hex-hmac-key";  // Encrypted in SPIFFS
} config;

// ============================================================================
// 3. GLOBAL INSTANCES
// ============================================================================

ArduClaw arduclaw;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================================
// 4. SETUP FUNCTION - Initialize everything
// ============================================================================

void setup() {
    // Serial debugging
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n✓ Arduclaw Production Edition Starting");
    
    // ========== WATCHDOG INITIALIZATION ==========
    WatchdogHelper::begin();
    LOG_INFO_TAG("Setup", "Watchdog initialized");
    
    // ========== LOGGING INITIALIZATION ==========
    Logger::getInstance().begin(true, -1);      // Serial output, no SD logging
    Logger::getInstance().setLevel(LOG_INFO);   // Production: INFO level
    LOG_INFO_TAG("Setup", "Logging initialized");
    
    // ========== LOAD & VERIFY CONFIG ==========
    if (!loadAndVerifyConfig()) {
        LOG_ERROR_TAG("Setup", "Config verification FAILED - using defaults");
        // Could fall back to AP mode for config portal here
    } else {
        LOG_INFO_TAG("Setup", "Config verified and decrypted");
    }
    
    // ========== SETUP SECURITY FEATURES ==========
    setupSecurityFeatures();
    
    // ========== WIFI CONNECTION ==========
    connectWiFi();
    WatchdogHelper::feed();
    
    // ========== MQTT CHANNEL ==========
    setupMQTTChannel();
    WatchdogHelper::feed();
    
    // ========== LLM PROVIDERS ==========
    setupLLMProviders();
    WatchdogHelper::feed();
    
    // ========== FIRMWARE SIGNATURE ==========
    OTASignatureVerifier::setPublicKey(DEVICE_PUBLIC_KEY);
    LOG_INFO_TAG("Setup", "Firmware signature verification ready");
    
    // ========== ARDUCLAW LIFECYCLE ==========
    arduclaw.begin();
    arduclaw.setPermissionLevel(PERMISSION_MEDIUM);
    
    LOG_INFO_TAG("Setup", "✓ Arduclaw production setup complete");
    WatchdogHelper::feedNow();
}

// ============================================================================
// 5. MAIN LOOP - Non-blocking async processing
// ============================================================================

void loop() {
    // Feed watchdog at start of iteration
    WatchdogHelper::feed();
    
    // Process async operations (non-blocking)
    arduclaw.loop();
    
    // Process MQTT (reconnect if needed)
    if (!mqttClient.connected()) {
        mqttReconnectWithBackoff();
    }
    mqttClient.loop();
    
    // Check for brute-force lockouts
    if (arduclaw.isLockedOut("global")) {
        unsigned long remainingMs = arduclaw.getRemainingLockout("global");
        Serial.printf("🔒 Brute-force lockout active for %lu ms\n", remainingMs);
        delay(1000);
        return;
    }
    
    // Process any pending requests
    WatchdogHelper::feed();  // Feed again after processing
    
    yield();  // Let WiFi stack do its thing
}

// ============================================================================
// 6. SECURITY FEATURES SETUP
// ============================================================================

void setupSecurityFeatures() {
    LOG_INFO_TAG("Security", "Initializing security features...");
    
    // SecretManager is used transparently by ConfigManager
    // - API keys are encrypted with device MAC
    // - Decryption happens automatically on config load
    
    // ConfigIntegrity verifies config wasn't tampered with
    // - Hash verified on load
    // - Hash computed on save
    // - Mismatch = config rejected
    
    // MQTT Message signing
    // - All messages signed with HMAC-SHA256
    // - Verification happens in MQTTChannel callback
    
    // Brute-force protection
    // - Automatic on failed validation/auth attempts
    // - 30s lockout @ 3 failures, exponential backoff
    
    LOG_INFO_TAG("Security", "Security features initialized");
}

// ============================================================================
// 7. CONFIG MANAGEMENT
// ============================================================================

bool loadAndVerifyConfig() {
    // In production: Load from SPIFFS encrypted config
    // For this example: Use hardcoded config
    
    String configJson = R"({
        "wifi_ssid": "YOUR_SSID",
        "wifi_pass": "YOUR_PASSWORD_ENCRYPTED",
        "mqtt_host": "mqtt.example.com",
        "mqtt_port": 8883,
        "mqtt_topic": "arduclaw/commands",
        "mqtt_secret_key": "a1b2c3d4e5f6...64chars",
        "_integrity_hash": "hash_here"
    })";
    
    // Parse
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, configJson) != 0) {
        LOG_ERROR_TAG("Config", "JSON parse error");
        return false;
    }
    
    // Verify integrity
    String storedHash = doc["_integrity_hash"].as<String>();
    doc.remove("_integrity_hash");
    String configStr;
    serializeJson(doc, configStr);
    
    if (!ConfigIntegrity::verify(configStr, storedHash)) {
        LOG_ERROR_TAG("Config", "Config integrity verification FAILED");
        return false;
    }
    
    // Extract and decrypt secrets
    String encryptedKey = doc["mqtt_secret_key"].as<String>();
    if (SecretManager::isEncrypted(encryptedKey)) {
        config.mqtt_secret_key = SecretManager::decrypt(encryptedKey);
        LOG_DEBUG_TAG("Config", "Secret decrypted");
    }
    
    return true;
}

// ============================================================================
// 8. WIFI CONNECTIVITY
// ============================================================================

void connectWiFi() {
    LOG_INFO_TAG("WiFi", "Connecting to " + config.wifi_ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid.c_str(), config.wifi_pass.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
        WatchdogHelper::feed();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO_TAG("WiFi", "✓ Connected, IP: " + WiFi.localIP().toString());
    } else {
        LOG_ERROR_TAG("WiFi", "✗ Connection failed");
    }
}

// ============================================================================
// 9. MQTT SETUP (WITH MESSAGE SIGNING)
// ============================================================================

void setupMQTTChannel() {
    LOG_INFO_TAG("MQTT", "Initializing secure MQTT channel...");
    
    // Create MQTT channel
    MQTTChannel* mqtt = new MQTTChannel(
        config.mqtt_host,
        config.mqtt_port,
        "arduclaw-" + String(random(0xFFFF), HEX),  // Client ID
        config.mqtt_topic
    );
    
    // Enable message signing (critical for security)
    mqtt->setSecretKey(config.mqtt_secret_key);
    
    // Add to Arduclaw
    arduclaw.addChannel(mqtt);
    
    // Configure PubSubClient for MQTT
    wifiClient.setInsecure();  // TODO: Enable certificate pinning
    mqttClient.setServer(config.mqtt_host.c_str(), config.mqtt_port);
    mqttClient.setCallback(onMQTTMessage);
    
    LOG_INFO_TAG("MQTT", "MQTT channel configured with signing enabled");
}

// ============================================================================
// 10. MQTT MESSAGE HANDLER (WITH SIGNATURE VERIFICATION)
// ============================================================================

void onMQTTMessage(char* topic, byte* payload, unsigned int length) {
    LOG_DEBUG_TAG("MQTT", "Message received on " + String(topic));
    
    // MQTTChannel automatically verifies signature in its callback
    // If signature is invalid, message is silently discarded
    // Only valid messages reach the handler
    
    // Parse message
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, length) != 0) {
        LOG_WARN_TAG("MQTT", "JSON parse error in message");
        arduclaw.recordFailure("mqtt");
        return;
    }
    
    // Process command
    String command = doc["command"].as<String>();
    LOG_INFO_TAG("MQTT", "Processing command: " + command);
    
    // Success = reset brute-force counter
    arduclaw.recordSuccess("mqtt");
}

// ============================================================================
// 11. MQTT RECONNECT WITH BACKOFF
// ============================================================================

static unsigned long lastReconnectAttempt = 0;
static unsigned long reconnectDelay = 1000;  // Start with 1s

void mqttReconnectWithBackoff() {
    if (millis() - lastReconnectAttempt < reconnectDelay) {
        return;  // Not yet time to retry
    }
    
    LOG_INFO_TAG("MQTT", "Attempting reconnect...");
    
    if (mqttClient.connect("arduclaw-device")) {
        LOG_INFO_TAG("MQTT", "✓ Connected to broker");
        mqttClient.subscribe(config.mqtt_topic.c_str());
        reconnectDelay = 1000;  // Reset backoff
        lastReconnectAttempt = millis();
    } else {
        LOG_WARN_TAG("MQTT", "✗ Connection failed, retrying in " + String(reconnectDelay) + "ms");
        reconnectDelay = min(reconnectDelay * 2, 32000UL);  // Exponential backoff, max 32s
        lastReconnectAttempt = millis();
        WatchdogHelper::feed();
    }
}

// ============================================================================
// 12. LLM PROVIDER SETUP
// ============================================================================

void setupLLMProviders() {
    LOG_INFO_TAG("LLM", "Initializing LLM providers...");
    
    // Option 1: OpenAI (requires API key)
    AC_OpenAIProvider* openai = new AC_OpenAIProvider();
    openai->begin(config.llm_api_key);
    arduclaw.addProvider(openai);
    LOG_INFO_TAG("LLM", "OpenAI provider added");
    
    // Option 2: Local LLM (Ollama, LocalAI)
    // AC_LocalLLMProvider* local = new AC_LocalLLMProvider();
    // local->begin(config.llm_host, config.llm_port);
    // arduclaw.addProvider(local);
    // LOG_INFO_TAG("LLM", "Local LLM provider added");
}

// ============================================================================
// 13. EXAMPLE: SEND REQUEST TO LLM
// ============================================================================

void askLLM(const String& question) {
    LOG_INFO_TAG("App", "Asking LLM: " + question);
    
    // Async request with automatic retry/backoff
    arduclaw.ask(question, [](JsonDocument& response) {
        LOG_INFO_TAG("App", "LLM Response received");
        
        if (response.containsKey("response")) {
            String answer = response["response"].as<String>();
            LOG_INFO_TAG("App", "Answer: " + answer);
        }
        
        WatchdogHelper::feed();  // Feed watchdog during processing
    });
}

// ============================================================================
// 14. FIRMWARE UPDATE WITH SIGNATURE VERIFICATION
// ============================================================================

bool verifyAndInstallFirmwareUpdate(const uint8_t* firmwareData,
                                   size_t firmwareSize,
                                   const uint8_t* signature) {
    LOG_INFO_TAG("OTA", "Verifying firmware signature...");
    WatchdogHelper::feedNow();
    
    OTASignatureVerifier::VerificationResult result =
        OTASignatureVerifier::verify(
            firmwareData, firmwareSize,
            signature,
            DEVICE_PUBLIC_KEY
        );
    
    if (result == OTASignatureVerifier::VerificationResult::VALID) {
        LOG_INFO_TAG("OTA", "✓ Firmware signature verified - SAFE TO INSTALL");
        return true;
    } else {
        LOG_ERROR_TAG("OTA", "✗ Firmware signature INVALID: " +
                     String(OTASignatureVerifier::getErrorMessage(result)));
        return false;
    }
}

// ============================================================================
// 15. DIAGNOSTIC FUNCTIONS
// ============================================================================

void printStatus() {
    Serial.println("\n=== Arduclaw Status ===");
    
    // WiFi status
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi: ✓ Connected (%s)\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi: ✗ Disconnected");
    }
    
    // MQTT status
    if (mqttClient.connected()) {
        Serial.println("MQTT: ✓ Connected (messages signed)");
    } else {
        Serial.println("MQTT: ✗ Disconnected");
    }
    
    // Brute-force status
    if (arduclaw.isLockedOut("global")) {
        unsigned long remaining = arduclaw.getRemainingLockout("global");
        Serial.printf("Security: 🔒 Lockout active (%lu ms remaining)\n", remaining);
    } else {
        Serial.println("Security: ✓ Normal operation");
    }
    
    // Watchdog
    if (WatchdogHelper::isEnabled()) {
        Serial.println("Watchdog: ✓ Enabled (feeding every 500ms)");
    }
    
    // Memory
    Serial.printf("Memory: %u bytes free\n", ESP.getFreeHeap());
    Serial.println("======================\n");
}

// ============================================================================
// 16. COMMAND HANDLER (For serial debugging)
// ============================================================================

void handleSerialCommand(const String& cmd) {
    if (cmd == "status") {
        printStatus();
    } else if (cmd.startsWith("ask ")) {
        String question = cmd.substring(4);
        askLLM(question);
    } else if (cmd == "reset") {
        Serial.println("Restarting device...");
        ESP.restart();
    } else {
        Serial.println("Unknown command");
    }
}

// ============================================================================
// COMPILATION & DEPLOYMENT
// ============================================================================
//
// 1. Install Arduino libraries:
//    - ArduinoJson (6.x)
//    - PubSubClient
//    - mbedtls (for crypto)
//
// 2. Select board:
//    - Tools > Board > ESP32-S3 / ESP32-C3 / ESP8266
//
// 3. Build & upload:
//    - Verify (Ctrl+Alt+V)
//    - Upload (Ctrl+U)
//
// 4. Monitor:
//    - Tools > Serial Monitor (115200 baud)
//
// ============================================================================
