#include "AC_LocalLLMProvider.h"
#include "../core/Logger.h"
#include "../core/MCUConfig.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

AC_LocalLLMProvider::AC_LocalLLMProvider(
    String h,
    uint16_t p,
    String e,
    String m
) : host(h), port(p), endpoint(e), model(m) {}

void AC_LocalLLMProvider::sendAsync(
    const String& prompt,
    std::function<void(String)> onSuccess,
    std::function<void(String)> onFailure
) {
    xTaskCreatePinnedToCore(
        [](void* param) {
            auto* args = (std::tuple<
                AC_LocalLLMProvider*, String,
                std::function<void(String)>,
                std::function<void(String)>>*)param;

            AC_LocalLLMProvider* self = std::get<0>(*args);
            String prompt = std::get<1>(*args);
            auto onSuccess = std::get<2>(*args);
            auto onFailure = std::get<3>(*args);

            WiFiClient client;
            HTTPClient http;
            http.setTimeout(HTTP_TIMEOUT_MS);

            String url = "http://" + self->host + ":" + String(self->port) + self->endpoint;

            LOG_DEBUG_TAG("LocalLLM", "Sending request to " + url);

            http.begin(client, url);
            http.addHeader("Content-Type", "application/json");

            StaticJsonDocument<JSON_BUFFER_SIZE_SM> req;
            req["model"] = self->model;
            req["prompt"] = prompt;
            req["stream"] = false;

            String body;
            serializeJson(req, body);

            int code = http.POST(body);

            if (code == 200) {
                String response = http.getString();
                StaticJsonDocument<JSON_BUFFER_SIZE_LG> resp;
                DeserializationError err = deserializeJson(resp, response);

                if (!err && resp.containsKey("response")) {
                    LOG_DEBUG_TAG("LocalLLM", "Response received");
                    if (onSuccess) onSuccess(resp["response"].as<String>());
                } else {
                    LOG_WARN_TAG("LocalLLM", "Invalid response format");
                    if (onFailure) onFailure("Invalid response format");
                }
            } else {
                String errMsg = "HTTP error: " + String(code);
                LOG_WARN_TAG("LocalLLM", errMsg);
                if (onFailure) onFailure(errMsg);
            }

            http.end();

            delete args;
            vTaskDelete(NULL);
        },
        "AC_LocalLLM",
        PROVIDER_STACK_SIZE,
        new std::tuple<
            AC_LocalLLMProvider*, String,
            std::function<void(String)>,
            std::function<void(String)>>(
            this, prompt, onSuccess, onFailure),
        1,
        NULL,
        1
    );
}