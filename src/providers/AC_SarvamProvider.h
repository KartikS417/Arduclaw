#ifndef AC_SARVAM_PROVIDER_H
#define AC_SARVAM_PROVIDER_H

#include <functional>
#include "AC_LLMProvider.h"

class AC_SarvamProvider : public AC_LLMProvider {
public:
    bool begin(const String& apiKey) override;
    void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    ) override;

private:
    String _apiKey;
};

#endif
