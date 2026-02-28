#include "ProviderManager.h"
#include "Logger.h"
#include "ErrorCodes.h"
#include "StructuredLogger.h"

ProviderManager::ProviderManager()
    : _providerCount(0), _activeIndex(0), _busy(false), _rateLimiter(1000)
{
}

ProviderManager::~ProviderManager()
{
}

void ProviderManager::addProvider(BaseProvider* provider)
{
    if (provider && _providerCount < MAX_PROVIDERS) {
        _providers[_providerCount] = provider;
        _metrics[_providerCount] = ProviderMetrics();
        _providerCount++;
        LOG_INFO_TAG("ProviderManager", "Provider added. Total: " + String(_providerCount));
    }
}

bool ProviderManager::hasProvider() const
{
    return _providerCount > 0;
}

bool ProviderManager::isBusy() const
{
    return _busy;
}

BaseProvider* ProviderManager::getActiveProvider()
{
    if (!hasProvider())
        return nullptr;

    return _providers[_activeIndex];
}

void ProviderManager::loop()
{
    if (!hasProvider())
        return;

    BaseProvider* provider = _providers[_activeIndex];

    if (provider)
        provider->loop();

    // Process async request queue
    _requestQueue.process();
}

void ProviderManager::sendAsync(
    const String& prompt,
    std::function<void(String)> onSuccess,
    std::function<void(String)> onFailure)
{
    if (_busy) {
        LOG_WARN_TAG("ProviderManager", "Provider busy");
        if (onFailure) onFailure("Provider busy");
        return;
    }

    if (!_rateLimiter.allow()) {
        LOG_WARN_TAG("ProviderManager", "Rate limit exceeded");
        if (onFailure) onFailure("Rate limit exceeded");
        return;
    }

    _busy = true;
    _metrics[_activeIndex].lastRequestTime = millis();

    BaseProvider* provider = _providers[_activeIndex];

    if (!provider)
    {
        _busy = false;
        if (onFailure) onFailure("No active provider");
        return;
    }

    provider->sendAsync(
        prompt,
        [this, onSuccess](String response)
        {
            _busy = false;
            markSuccess();
            if (onSuccess) onSuccess(response);
        },
        [this, onFailure](String error)
        {
            _busy = false;
            markFailure();
            if (onFailure) onFailure(error);
        }
    );
}

int ProviderManager::sendAsyncWithRetry(
    const String& prompt,
    std::function<void(String, ErrorCode)> onSuccess,
    std::function<void(ErrorCode, const String&)> onFailure,
    const RequestConfig& config)
{
    if (!_rateLimiter.allow()) {
        LOG_WARN_TAG("ProviderManager", "Rate limit exceeded");
        int requestId = RequestTracker::getInstance().createRequest("ProviderManager", prompt);
        RequestTracker::getInstance().updateState(requestId, RequestState::FAILED, 
                                                  ErrorCode::RATE_LIMIT_EXCEEDED);
        if (onFailure) onFailure(ErrorCode::RATE_LIMIT_EXCEEDED, "Rate limit exceeded");
        return requestId;
    }

    BaseProvider* provider = _providers[_activeIndex];
    if (!provider) {
        LOG_ERROR_TAG("ProviderManager", "No active provider");
        int requestId = RequestTracker::getInstance().createRequest("ProviderManager", prompt);
        RequestTracker::getInstance().updateState(requestId, RequestState::FAILED, 
                                                  ErrorCode::PROVIDER_NOT_FOUND);
        if (onFailure) onFailure(ErrorCode::PROVIDER_NOT_FOUND, "No active provider");
        return requestId;
    }

    _metrics[_activeIndex].lastRequestTime = millis();

    return _requestQueue.enqueueRequest(prompt, provider, onSuccess, onFailure, config);
}

int ProviderManager::streamAsync(
    const String& prompt,
    std::function<void(const String& chunk)> onChunk,
    std::function<void(ErrorCode)> onComplete,
    std::function<void(ErrorCode, const String&)> onFailure,
    const RequestConfig& config)
{
    if (!_rateLimiter.allow()) {
        LOG_WARN_TAG("ProviderManager", "Rate limit exceeded for streaming");
        int requestId = RequestTracker::getInstance().createRequest("ProviderManager", prompt);
        RequestTracker::getInstance().updateState(requestId, RequestState::FAILED,
                                                  ErrorCode::RATE_LIMIT_EXCEEDED);
        if (onFailure) onFailure(ErrorCode::RATE_LIMIT_EXCEEDED, "Rate limit exceeded");
        return requestId;
    }

    BaseProvider* provider = _providers[_activeIndex];
    if (!provider) {
        LOG_ERROR_TAG("ProviderManager", "No active provider for streaming");
        int requestId = RequestTracker::getInstance().createRequest("ProviderManager", prompt);
        RequestTracker::getInstance().updateState(requestId, RequestState::FAILED,
                                                  ErrorCode::PROVIDER_NOT_FOUND);
        if (onFailure) onFailure(ErrorCode::PROVIDER_NOT_FOUND, "No active provider");
        return requestId;
    }

    _metrics[_activeIndex].lastRequestTime = millis();

    return _requestQueue.enqueueStreamRequest(prompt, provider, onChunk, onComplete, onFailure, config);
}

void ProviderManager::markSuccess()
{
    if (_activeIndex < _providerCount) {
        _metrics[_activeIndex].successCount++;
        LOG_DEBUG_TAG("ProviderManager", "Success marked. Rate: " + 
                     String(_metrics[_activeIndex].successRate(), 1) + "%");
    }
}

void ProviderManager::markFailure()
{
    if (_activeIndex < _providerCount) {
        _metrics[_activeIndex].failureCount++;
        LOG_DEBUG_TAG("ProviderManager", "Failure marked. Rate: " + 
                     String(_metrics[_activeIndex].successRate(), 1) + "%");
    }
    switchToNextProvider();
}

void ProviderManager::switchToNextProvider()
{
    if (_providerCount == 0)
        return;

    int previousIndex = _activeIndex;
    _activeIndex = (_activeIndex + 1) % _providerCount;
    
    LOG_WARN_TAG("ProviderManager", "Switching from provider " + String(previousIndex) + 
                " to " + String(_activeIndex));
}

bool ProviderManager::setActiveProvider(const String& name)
{
    for (int i = 0; i < (int)_providerCount; ++i) {
        BaseProvider* p = _providers[i];
        if (p && p->getProviderName() == name) {
            _activeIndex = i;
            return true;
        }
    }
    return false;
}

bool ProviderManager::hasActiveProvider() const
{
    return _activeIndex < _providerCount && _providers[_activeIndex] != nullptr;
}

const ProviderMetrics& ProviderManager::getMetrics() const
{
    if (_activeIndex < _providerCount) {
        return _metrics[_activeIndex];
    }
    static ProviderMetrics empty;
    return empty;
}

String ProviderManager::getStatus() const
{
    if (!hasProvider()) {
        return "No providers";
    }
    const ProviderMetrics& m = getMetrics();
    return "Provider " + String(_activeIndex) + 
           " | Success: " + String(m.successCount) + 
           " | Failure: " + String(m.failureCount) + 
           " | Rate: " + String(m.successRate(), 1) + "%";
}