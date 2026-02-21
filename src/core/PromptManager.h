#pragma once
#include <Arduino.h>

class PromptManager {
public:
    static String build(const String& userPrompt) {

        return String(
            "You are an embedded automation agent.\n"
            "Return STRICT JSON only.\n"
            "No explanation.\n"
            "{ \"action\": \"name\", \"field\": value }\n\n"
        ) + userPrompt;
    }
};
