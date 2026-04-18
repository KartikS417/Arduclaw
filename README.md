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


Tips
- Use a config portal (WiFiManager) for field setup to improve UX.
- Feed the watchdog inside long waits and retry loops.
- Log clear status messages and expose IP/address in serial output for easy debugging.

**Dependencies**
- ArduinoJson (6.x)
- PubSubClient (for MQTT examples)
- mbedTLS or platform crypto libs (for signature verification)

**Examples**
- `examples/ESP32_AI_ChatTerminal/` — simple serial-driven agent that controls hardware
- `examples/TelegramLEDControl/` — simple llm driven builtin led control agent
- `examples/TelegramBeep/` — llm driven beep control in Waveshare ESP32S3TouchLCD1.83 from Telegram Chat
- `examples/ESP32_S3_TouchLCD183_VoiceAssistant/` — llm driven voice assistant for ESP32S3TouchLCD1.83

**Security & Production Notes**
- Secrets: stored encrypted (SecretManager) and decrypted at runtime when mounted.
- Config integrity: verified by `ConfigIntegrity` before applying.
- OTA: `OTASignatureVerifier` assists in validating signed firmware images.

**Developer notes**
- In-tree (development): examples can be compiled directly from the repo.
- Installed (library mode): include `#include <Arduclaw.h>` and `#include <providers/AC_LocalLLMProvider.h>` in your sketches.

**Contributing**
- Issues & PRs welcome. When reporting runtime or build issues include your target board, core version, and the example used.

**License**
- This project is released under the MIT License — see [LICENSE](LICENSE) for details.
![](https://img.shields.io/badge/license-MIT-brightgreen)

**Contact**
- Open an issue or PR in this repository for questions or help.
