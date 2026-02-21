# Arduclaw — LLM Agents for Microcontrollers

Arduclaw is a compact, production-minded Arduino library that brings LLM-driven agents, secure channels, and device-focused tooling to constrained microcontrollers (ESP32/ESP8266 and similar).

**Why Arduclaw?**
- **Edge-first:** lightweight abstractions for local and remote LLM providers.
- **Secure by design:** secrets management, config integrity, signed OTA and message verification.
- **Modular:** swap channels (MQTT/HTTP/Serial) and LLM providers with minimal changes.
- **Production-ready:** watchdog integration, rate limiting, brute-force protection, and OTA helpers.

**Highlights**
- Provider support: OpenAI + local LLM adapters (`AC_OpenAIProvider`, `AC_LocalLLMProvider`).
- Channels: secure MQTT, HTTP clients, and serial channel for debugging/CLI.
- Core features: request tracking, prompt manager, action registry, and permission levels.
- Examples: ready-to-run sketches demonstrating Basic agent, Local LLM usage, SPIFFS config, and MQTT config portal.

**Repository Layout**
- `src/` — library source: core, providers, channels, utils
- `examples/` — Arduino sketches demonstrating typical flows
- `data/` — sample runtime config (e.g., `llm_config.json`)
- `library.properties` — Arduino library metadata

**Getting started**
1. Install dependencies in the Arduino IDE / PlatformIO (see Dependencies below).
2. Copy the project into Arduino `libraries/` (or compile examples in-tree using the included wrappers).
3. Open an example from `examples/`, edit WiFi / provider config, and upload to your board.

```cpp
// Example includes (library-style)
#include <Arduclaw.h>
#include <providers/AC_LocalLLMProvider.h>
```

**Supported Boards**
- ESP32, ESP32-S2/S3, ESP8266 (features like SPIFFS/OTA may vary by board).

**Wi‑Fi Guide**
- Initialize Serial early: call `Serial.begin()` before operations that log status.
- Store credentials securely: prefer SPIFFS or `data/llm_config.json` over hardcoding.
- Non-blocking connect: avoid long blocking loops; use timeouts and feed the watchdog while waiting.
- Use event handlers: `WiFi.onEvent()` (ESP32) to react to `SYSTEM_EVENT_STA_GOT_IP` and `SYSTEM_EVENT_STA_DISCONNECTED`.
- TLS and verification: prefer `WiFiClientSecure` with certificate pinning or CA verification for LLM endpoints and MQTT brokers.
- Reconnect strategy: exponential backoff with a capped delay; avoid tight retry loops that starve other tasks.

Snippet — robust connect + reconnect (ESP32):
```cpp
void startWiFi(const char* ssid, const char* pass) {
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, pass);
	Serial.print("Connecting to WiFi");
	unsigned long start = millis();
	const unsigned long timeout = 15000; // 15s
	while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout) {
		delay(200);
		yield();
	}
	if (WiFi.status() == WL_CONNECTED) {
		Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
	} else {
		Serial.println("\nWiFi connect timed out");
		// Consider starting AP/config portal here
	}
}

void handleWiFiReconnect() {
	static unsigned long lastAttempt = 0;
	static unsigned long backoff = 1000;
	if (WiFi.status() != WL_CONNECTED) {
		if (millis() - lastAttempt >= backoff) {
			lastAttempt = millis();
			WiFi.reconnect();
			backoff = min(backoff * 2, 30000UL);
		}
	} else {
		backoff = 1000; // reset when connected
	}
}
```

Tips
- Use a config portal (WiFiManager) for field setup to improve UX.
- Feed the watchdog inside long waits and retry loops.
- Log clear status messages and expose IP/address in serial output for easy debugging.

**Dependencies**
- ArduinoJson (6.x)
- PubSubClient (for MQTT examples)
- mbedTLS or platform crypto libs (for signature verification)

**Examples**
- `examples/BasicAgent/` — simple serial-driven agent that controls hardware
- `examples/LocalLLMAgent/` — use a local LLM endpoint (configurable via SPIFFS)
- `examples/LocalLLMAgentSPIFFS/` — read LLM settings from SPIFFS and run local provider
- `examples/MQTT_ConfigPortal/` — receive config via MQTT and persist to SPIFFS
- `examples/ProductionExample.ino` — integration guide demonstrating security and production patterns

**Security & Production Notes**
- Secrets: stored encrypted (SecretManager) and decrypted at runtime when mounted.
- Config integrity: verified by `ConfigIntegrity` before applying.
- OTA: `OTASignatureVerifier` assists in validating signed firmware images.

**Developer notes**
- In-tree (development): examples can be compiled directly from the repo. The project also includes wrapper headers so examples compile both in-tree and after installation to Arduino `libraries/`.
- Installed (library mode): include `#include <Arduclaw.h>` and `#include <providers/AC_LocalLLMProvider.h>` in your sketches.

**Contributing**
- Issues & PRs welcome. When reporting runtime or build issues include your target board, core version, and the example used.

**License**
- This project is released under the MIT License — see [LICENSE](LICENSE) for details.
![](https://img.shields.io/badge/license-MIT-brightgreen)

**Contact**
- Open an issue or PR in this repository for questions or help.
