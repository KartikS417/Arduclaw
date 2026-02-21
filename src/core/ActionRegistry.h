#pragma once
#include <Arduino.h>
#include <vector>

enum PermissionLevel {
    PERMISSION_LOW = 0,
    PERMISSION_MEDIUM = 1,
    PERMISSION_HIGH = 2
};

struct RegisteredAction {
    String name;
    std::vector<String> requiredFields;
    PermissionLevel permission;
};

class ActionRegistry {
private:
    std::vector<RegisteredAction> actions;

public:
    void registerAction(String name,
                        std::vector<String> fields,
                        PermissionLevel perm) {

        actions.push_back({name, fields, perm});
    }

    const RegisteredAction* find(const String& name) {
        for (auto &a : actions)
            if (a.name == name) return &a;
        return nullptr;
    }
};
