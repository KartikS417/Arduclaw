#ifndef AC_SARVAM_PROVIDER_H
#define AC_SARVAM_PROVIDER_H

#include "AC_LLMProvider.h"

class AC_SarvamProvider : public AC_LLMProvider {
public:
    bool begin(const String& apiKey) override;
    String generate(const String& input) override;

private:
    String _apiKey;
};

#endif
