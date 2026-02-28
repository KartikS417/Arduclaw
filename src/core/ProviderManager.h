#ifndef PROVIDER_MANAGER_H
#define PROVIDER_MANAGER_H

#include "../providers/BaseProvider.h"
#include "RateLimiter.h"
#include "RequestTracker.h"
#include "AsyncRequestQueue.h"
#include "ErrorCodes.h"
#include "MCUConfig.h"

struct ProviderMetrics {
    int successCount = 0;
    int failureCount = 0;
    unsigned long lastRequestTime = 0;
    float successRate() const {
        int total = successCount + failureCount;
        return total == 0 ? 0 : (float)successCount / total * 100.0f;
    }
};

class ProviderManager
{
public:
    ProviderManager();
    ~ProviderManager();

    // Register providers
    void addProvider(BaseProvider* provider);

    // Call current active provider
    void sendAsync(
        const String& prompt,
        std::function<void(String)> onSuccess,
        std::function<void(String)> onFailure
    );

    // Non-blocking request with error codes and retry logic
    int sendAsyncWithRetry(
        const String& prompt,
        std::function<void(String, ErrorCode)> onSuccess,
        std::function<void(ErrorCode, const String&)> onFailure,
        const RequestConfig& config = RequestConfig()
    );

    // Non-blocking streaming request
    int streamAsync(
        const String& prompt,
        std::function<void(const String& chunk)> onChunk,
        std::function<void(ErrorCode)> onComplete,
        std::function<void(ErrorCode, const String&)> onFailure,
        const RequestConfig& config = RequestConfig()
    );

    // Called inside ArduClaw::loop()
    void loop();

    // Status
    bool isBusy() const;
    bool hasProvider() const;
    bool setActiveProvider(const String& name);
    bool hasActiveProvider() const;

    // Failover control
    void markSuccess();
    void markFailure();

    // Access
    BaseProvider* getActiveProvider();
    
    // Health metrics
    const ProviderMetrics& getMetrics() const;
    String getStatus() const;

    // Request tracking
    RequestStatus* getRequestStatus(int requestId) {
        return RequestTracker::getInstance().getStatus(requestId);
    }

    void cancelRequest(int requestId) {
        _requestQueue.cancelRequest(requestId);
    }

    uint8_t getProviderCount() const {
        return _providerCount;
    }

private:
    BaseProvider* _providers[MAX_PROVIDERS];
    ProviderMetrics _metrics[MAX_PROVIDERS];
    uint8_t _providerCount;
    int _activeIndex;
    bool _busy;
    RateLimiter _rateLimiter;
    AsyncRequestQueue _requestQueue;

    void switchToNextProvider();
};

#endif