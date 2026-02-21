#pragma once

#include <functional>
#include <ArduinoJson.h>

#include "core/ProviderManager.h"
#include "core/ActionRegistry.h"
#include "core/JsonValidator.h"
#include "core/PromptManager.h"
#include "core/ChannelManager.h"
#include "core/RateLimiter.h"

class ArduClaw {
private:
    ProviderManager providerManager;
    ActionRegistry registry;
    ChannelManager channelManager;
    BruteForceProtector bruteForceProtector;

    PermissionLevel runtimePermission = PERMISSION_MEDIUM;

    std::function<void(JsonDocument&)> _actionHandler;

public:
    // ========================
    // Security - Brute Force Protection
    // ========================

    bool isLockedOut(const char* source) {
        return bruteForceProtector.isLockedOut(source);
    }

    unsigned long getRemainingLockout(const char* source) {
        return bruteForceProtector.getRemainingLockout(source);
    }

    bool recordFailure(const char* source) {
        return bruteForceProtector.recordFailure(source);
    }

    void recordSuccess(const char* source) {
        bruteForceProtector.recordSuccess(source);
    }

    // ========================
    // Provider
    // ========================

    void addProvider(BaseProvider* p) {
        providerManager.addProvider(p);
    }

    // ========================
    // Channels
    // ========================

    void addChannel(BaseChannel* c) {
        channelManager.add(c);
    }

    // ========================
    // Action Registration
    // ========================

    void registerAction(String name,
                        std::vector<String> fields,
                        PermissionLevel perm) {
        registry.registerAction(name, fields, perm);
    }

    void setPermissionLevel(PermissionLevel level) {
        runtimePermission = level;
    }

    void onAction(std::function<void(JsonDocument&)> handler) {
        _actionHandler = handler;
    }

    // ========================
    // Lifecycle
    // ========================

    void begin() {
        channelManager.begin();
        ConfigManager.isValid();
    }

    void loop();

    // ========================
    // Ask LLM
    // ========================

    void ask(String msg,
             std::function<void(JsonDocument&)> cb);
};
