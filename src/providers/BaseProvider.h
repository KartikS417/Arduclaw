#pragma once
#include <functional>
#include "../core/ErrorCodes.h"

struct RequestConfig {
    unsigned long timeoutMs = 10000;      // Default 10 second timeout
    int maxRetries = 2;                   // Default 2 retries
    unsigned long retryDelayMs = 1000;    // Default 1 second delay
    bool exponentialBackoff = true;       // Increase delay after each retry
    
    RequestConfig() {}
    RequestConfig(unsigned long timeout, int retries = 2) 
        : timeoutMs(timeout), maxRetries(retries) {}
};

class BaseProvider {
public:
    virtual ~BaseProvider() {}

    // Basic async method (legacy support)
    virtual void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    ) = 0;

    // Enhanced async method with error codes and retry logic
    virtual void sendAsyncWithRetry(
        const String& prompt,
        std::function<void(String, ErrorCode)> onSuccess,
        std::function<void(ErrorCode, const String&)> onFailure,
        const RequestConfig& config = RequestConfig()
    ) {
        // Default implementation falls back to basic sendAsync
        sendAsync(prompt,
            [onSuccess](String result) {
                onSuccess(result, ErrorCode::SUCCESS);
            },
            [onFailure](String error) {
                onFailure(ErrorCode::PROVIDER_FAILED, error);
            }
        );
    }

    // Lifecycle methods
    virtual void begin() {}
    virtual void loop() {}
    virtual void end() {}

    // Status methods
    virtual bool isReady() { return true; }
    virtual bool isBusy() { return false; }
    
    // Get provider name/type
    virtual String getProviderName() { return "BaseProvider"; }
};
