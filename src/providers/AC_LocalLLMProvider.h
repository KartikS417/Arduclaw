#ifndef AC_LOCAL_LLM_PROVIDER_H
#define AC_LOCAL_LLM_PROVIDER_H

#include <functional>
#include "AC_LLMProvider.h"

class AC_LocalLLMProvider : public AC_LLMProvider {
public:
    AC_LocalLLMProvider(String host, uint16_t port, String endpoint, String model);

    bool begin(const String& apiKey) override;
    
    void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    ) override;

private:
    String _host;
    uint16_t _port;
    String _endpoint;
    String _model;
    String _apiKey;
};

#endif