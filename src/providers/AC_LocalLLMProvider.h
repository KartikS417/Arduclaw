#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include "../core/BaseProvider.h"

class AC_LocalLLMProvider : public BaseProvider {

private:
    String host;
    uint16_t port;
    String endpoint;
    String model;

public:
    AC_LocalLLMProvider(
        String host,
        uint16_t port,
        String endpoint,
        String model
    );

    void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    ) override;
};