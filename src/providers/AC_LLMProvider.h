#ifndef AC_LLM_PROVIDER_H
#define AC_LLM_PROVIDER_H

#include <Arduino.h>
#include <functional>

class AC_LLMProvider {
public:
    // Virtual begin with default implementation (some providers like LocalLLM might not need an API key)
    virtual bool begin(const String& apiKey) { return true; }

    // Pure virtual async generation method
    virtual void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    ) = 0;

    virtual ~AC_LLMProvider() {}
};

#endif
