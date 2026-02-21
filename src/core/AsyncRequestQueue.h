#pragma once
#include <Arduino.h>
#include <functional>
#include "../providers/BaseProvider.h"
#include "ErrorCodes.h"
#include "RequestTracker.h"
#include "StructuredLogger.h"

class AsyncRequest {
public:
    int requestId;
    String prompt;
    BaseProvider* provider;
    RequestConfig config;
    int attemptCount;
    unsigned long startTime;
    unsigned long lastAttemptTime;
    
    std::function<void(String, ErrorCode)> onSuccess;
    std::function<void(ErrorCode, const String&)> onFailure;

    AsyncRequest(int id, const String& p, BaseProvider* prov, const RequestConfig& cfg)
        : requestId(id), prompt(p), provider(prov), config(cfg), 
          attemptCount(0), startTime(millis()), lastAttemptTime(0) {}

    bool shouldRetry() {
        if (attemptCount >= config.maxRetries) return false;
        
        unsigned long elapsed = millis() - startTime;
        if (elapsed > config.timeoutMs * 2) return false; // Global timeout
        
        unsigned long timeSinceLastAttempt = millis() - lastAttemptTime;
        unsigned long delayRequired = config.retryDelayMs;
        
        if (config.exponentialBackoff) {
            delayRequired = config.retryDelayMs * (1 << attemptCount); // 2^attemptCount
        }
        
        return timeSinceLastAttempt >= delayRequired;
    }

    void executeAttempt() {
        attemptCount++;
        lastAttemptTime = millis();

        RequestTracker::getInstance()
            .updateState(requestId, RequestState::IN_PROGRESS, ErrorCode::UNKNOWN);

        provider->sendAsync(prompt,
            [this](String result) {
                RequestTracker::getInstance()
                    .setResult(requestId, result);
                RequestTracker::getInstance()
                    .updateState(requestId, RequestState::SUCCESS, ErrorCode::SUCCESS);
                
                if (onSuccess) {
                    onSuccess(result, ErrorCode::SUCCESS);
                }

                LogEvent evt(EventType::PROVIDER_REQUEST_SUCCESS, ErrorCode::SUCCESS,
                           provider->getProviderName(), "Request completed");
                evt.context = "attempt=" + String(attemptCount);
                StructuredLogger::getInstance().logEvent(evt);
            },
            [this](String error) {
                if (shouldRetry()) {
                    RequestTracker::getInstance().incrementRetry(requestId);
                    
                    LogEvent evt(EventType::PROVIDER_RETRY, ErrorCode::PROVIDER_FAILED,
                               provider->getProviderName(), "Retrying request");
                    evt.context = "attempt=" + String(attemptCount) + 
                                 ",nextRetryMs=" + String(config.retryDelayMs);
                    StructuredLogger::getInstance().logEvent(evt);
                } else {
                    ErrorCode code = ErrorCode::MAX_RETRIES_EXCEEDED;
                    if (millis() - startTime > config.timeoutMs) {
                        code = ErrorCode::PROVIDER_TIMEOUT;
                    }

                    RequestTracker::getInstance()
                        .updateState(requestId, RequestState::FAILED, code);

                    if (onFailure) {
                        onFailure(code, error);
                    }

                    LogEvent evt(EventType::PROVIDER_REQUEST_FAILURE, code,
                               provider->getProviderName(), error);
                    evt.context = "attempts=" + String(attemptCount);
                    StructuredLogger::getInstance().logEvent(evt);
                }
            }
        );
    }
};

class AsyncRequestQueue {
private:
    static const int MAX_QUEUE = 32;
    std::vector<AsyncRequest*> _pendingRequests;

public:
    ~AsyncRequestQueue() {
        for (auto req : _pendingRequests) {
            delete req;
        }
    }

    int enqueueRequest(const String& prompt, BaseProvider* provider,
                      std::function<void(String, ErrorCode)> onSuccess,
                      std::function<void(ErrorCode, const String&)> onFailure,
                      const RequestConfig& config = RequestConfig()) {
        
        int requestId = RequestTracker::getInstance().createRequest("Provider", prompt);
        
        if (_pendingRequests.size() >= MAX_QUEUE) {
            RequestTracker::getInstance()
                .updateState(requestId, RequestState::FAILED, ErrorCode::MEMORY_INSUFFICIENT);
            if (onFailure) {
                onFailure(ErrorCode::MEMORY_INSUFFICIENT, "Queue full");
            }
            return -1;
        }

        AsyncRequest* req = new AsyncRequest(requestId, prompt, provider, config);
        req->onSuccess = onSuccess;
        req->onFailure = onFailure;
        
        _pendingRequests.push_back(req);
        return requestId;
    }

    void process() {
        std::vector<int> completed;

        for (size_t i = 0; i < _pendingRequests.size(); i++) {
            AsyncRequest* req = _pendingRequests[i];

            if (req->attemptCount == 0) {
                // First attempt
                req->executeAttempt();
            } else if (req->shouldRetry()) {
                // Retry
                req->executeAttempt();
            }

            // Check if complete
            RequestStatus* status = RequestTracker::getInstance().getStatus(req->requestId);
            if (status && status->isComplete()) {
                completed.push_back(i);
            }
        }

        // Remove completed requests
        for (int i = completed.size() - 1; i >= 0; i--) {
            delete _pendingRequests[completed[i]];
            _pendingRequests.erase(_pendingRequests.begin() + completed[i]);
        }
    }

    int getPendingCount() const {
        return _pendingRequests.size();
    }

    void cancelRequest(int requestId) {
        for (size_t i = 0; i < _pendingRequests.size(); i++) {
            if (_pendingRequests[i]->requestId == requestId) {
                RequestTracker::getInstance()
                    .updateState(requestId, RequestState::CANCELLED, ErrorCode::UNKNOWN);
                delete _pendingRequests[i];
                _pendingRequests.erase(_pendingRequests.begin() + i);
                return;
            }
        }
    }

    void cancelAll() {
        for (auto req : _pendingRequests) {
            RequestTracker::getInstance()
                .updateState(req->requestId, RequestState::CANCELLED, ErrorCode::UNKNOWN);
            delete req;
        }
        _pendingRequests.clear();
    }
};
