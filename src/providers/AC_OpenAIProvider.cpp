#include "AC_OpenAIProvider.h"
#include "../core/Logger.h"
#include "../core/MCUConfig.h"

void AC_OpenAIProvider::sendAsync(
    const String& prompt,
    std::function<void(String)> onSuccess,
    std::function<void(String)> onFailure) {

    xTaskCreatePinnedToCore(
        [](void* param) {

            auto* args = (std::tuple<
                AC_OpenAIProvider*, String,
                std::function<void(String)>,
                std::function<void(String)>>*)param;

            AC_OpenAIProvider* self =
                std::get<0>(*args);

            String prompt =
                std::get<1>(*args);

            auto onSuccess =
                std::get<2>(*args);

            auto onFailure =
                std::get<3>(*args);

            WiFiClientSecure client;
            client.setInsecure();

            HTTPClient http;
            http.setTimeout(HTTP_TIMEOUT_MS);  // Configurable per MCU
            http.begin(client,
                "https://api.openai.com/v1/chat/completions");

            http.addHeader("Content-Type",
                "application/json");

            http.addHeader("Authorization",
                "Bearer " + self->apiKey);

            // Use ArduinoJson for safe payload creation, just like in SarvamProvider
            StaticJsonDocument<JSON_BUFFER_SIZE_SM> doc;
            doc["model"] = "gpt-4o-mini";
            JsonArray messages = doc.createNestedArray("messages");
            JsonObject msg = messages.createNestedObject();
            msg["role"] = "user";
            msg["content"] = prompt;

            String payload;
            serializeJson(doc, payload);
            
            LOG_DEBUG_TAG("OpenAI", "Sending request to OpenAI API");

            int code = http.POST(payload);

            String result;

            if (code == 200) {
                WiFiClient* stream =
                    http.getStreamPtr();

                bool started = false;
                int braces = 0;

                while (stream->connected() ||
                       stream->available()) {

                    if (!stream->available())
                        continue;

                    char c = stream->read();

                    if (c == '{') {
                        started = true;
                        braces++;
                    }

                    if (started)
                        result += c;

                    if (c == '}') {
                        braces--;
                        if (braces == 0)
                            break;
                    }
                }
                LOG_DEBUG_TAG("OpenAI", "Response received");
                if (onSuccess) onSuccess(result);
            } else {
                String errMsg = "HTTP error: " + String(code);
                LOG_WARN_TAG("OpenAI", errMsg);
                if (onFailure) onFailure(errMsg);
            }

            http.end();

            delete args;
            vTaskDelete(NULL);

        },
        "AC_OpenAI",
        PROVIDER_STACK_SIZE,
        new std::tuple<
            AC_OpenAIProvider*, String,
            std::function<void(String)>,
            std::function<void(String)>>(
            this, prompt, onSuccess, onFailure),
        1,
        NULL,
        1
    );
}
