#include "AC_SarvamProvider.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../core/MCUConfig.h"

bool AC_SarvamProvider::begin(const String& apiKey) {
    _apiKey = apiKey;
    return true;
}

String AC_SarvamProvider::generate(const String& input) {

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);  // Configurable per MCU
    http.begin("https://api.sarvam.ai/v1/chat/completions");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + _apiKey);

    StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
    doc["model"] = "sarvam-m";

    JsonArray messages = doc.createNestedArray("messages");
    JsonObject msg = messages.createNestedObject();
    msg["role"] = "user";
    msg["content"] = input;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);

    if (code <= 0) {
        http.end();
        return "";
    }

    String response = http.getString();
    http.end();

    StaticJsonDocument<JSON_BUFFER_SIZE_LG> respDoc;
    if (deserializeJson(respDoc, response)) {
        return "";
    }

    return respDoc["choices"][0]["message"]["content"].as<String>();
}
