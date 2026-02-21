#pragma once
#include <ArduinoJson.h>
#include "ActionRegistry.h"

class JsonValidator {
public:
    static bool validate(const String& json,
                         ActionRegistry& registry,
                         JsonDocument& outDoc,
                         PermissionLevel maxAllowed) {

        DeserializationError err = deserializeJson(outDoc, json);
        if (err) return false;

        if (!outDoc.containsKey("action"))
            return false;

        String actionName = outDoc["action"].as<String>();

        const RegisteredAction* action =
            registry.find(actionName);

        if (!action)
            return false;

        // 🔒 Permission enforcement
        if (action->permission > maxAllowed)
            return false;

        // Required fields validation
        for (auto &field : action->requiredFields) {
            if (!outDoc.containsKey(field))
                return false;
        }

        return true;
    }
};
