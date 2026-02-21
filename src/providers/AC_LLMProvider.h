#ifndef AC_LLM_PROVIDER_H
#define AC_LLM_PROVIDER_H

#include <Arduino.h>

class AC_LLMProvider {
public:
    virtual bool begin(const String& apiKey) = 0;
    virtual String generate(const String& input) = 0;
    virtual ~AC_LLMProvider() {}
};

#endif
