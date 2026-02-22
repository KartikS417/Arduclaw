#include "AC_SarvamProvider.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../core/MCUConfig.h"
#include "../core/Logger.h"

bool AC_SarvamProvider::begin(const String& apiKey) {
    _apiKey = apiKey;
    return true;
}

void AC_SarvamProvider::sendAsync(
    const String& prompt,
    std::function<void(String)> onSuccess,
    std::function<void(String)> onFailure) {

    xTaskCreatePinnedToCore(
        [](void* param) {
            auto* args = (std::tuple<
                AC_SarvamProvider*, String,
                std::function<void(String)>,
                std::function<void(String)>>*)param;

            AC_SarvamProvider* self = std::get<0>(*args);
            String prompt = std::get<1>(*args);
            auto onSuccess = std::get<2>(*args);
            auto onFailure = std::get<3>(*args);

            WiFiClientSecure client;
            client.setInsecure();

            HTTPClient http;
            http.setTimeout(HTTP_TIMEOUT_MS);
            http.begin(client, "https://api.sarvam.ai/v1/chat/completions");
            http.addHeader("Content-Type", "application/json");
            http.addHeader("Authorization", "Bearer " + self->_apiKey);

            StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
            doc["model"] = "sarvam-m";

            JsonArray messages = doc.createNestedArray("messages");
            JsonObject msg = messages.createNestedObject();
            msg["role"] = "user";
            msg["content"] = prompt;

            String payload;
            serializeJson(doc, payload);

            LOG_DEBUG_TAG("Sarvam", "Sending request to Sarvam API");
            int code = http.POST(payload);

            if (code == 200) {
                String response = http.getString();
                StaticJsonDocument<JSON_BUFFER_SIZE_LG> respDoc;
                DeserializationError error = deserializeJson(respDoc, response);

                if (error) {
                    LOG_WARN_TAG("Sarvam", "JSON deserialization failed");
                    if (onFailure) onFailure("JSON deserialization failed");
                } else {
                    String result = respDoc["choices"][0]["message"]["content"].as<String>();
                    LOG_DEBUG_TAG("Sarvam", "Response received");
                    if (onSuccess) onSuccess(result);
                }
            } else {
                String errMsg = "HTTP error: " + String(code);
                LOG_WARN_TAG("Sarvam", errMsg);
                if (onFailure) onFailure(errMsg);
            }

            http.end();

            delete args;
            vTaskDelete(NULL);
        },
        "AC_Sarvam",
        PROVIDER_STACK_SIZE,
        new std::tuple<
            AC_SarvamProvider*, String,
            std::function<void(String)>,
            std::function<void(String)>>(
            this, prompt, onSuccess, onFailure),
        1,
        NULL,
        1
    );
}
