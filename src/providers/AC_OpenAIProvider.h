#pragma once
#include "BaseProvider.h"
#include "../core/MemoryManager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

class OpenAIProvider : public BaseProvider {
private:
    String apiKey;
    String endpoint;

    String extractJson(Stream& stream);

public:
    OpenAIProvider(String key);

    void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    ) override;
};
