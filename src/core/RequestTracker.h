#pragma once
#include <Arduino.h>
#include <functional>
#include "ErrorCodes.h"
#include "MCUConfig.h"

enum class RequestState {
    PENDING,
    IN_PROGRESS,
    SUCCESS,
    FAILED,
    TIMEOUT,
    CANCELLED
};

struct RequestStatus {
    int requestId;
    RequestState state;
    ErrorCode code;
    unsigned long createdAt;
    unsigned long startedAt;
    unsigned long completedAt;
    int retryCount;
    char tag[64];
    char prompt[256];
    char result[512];

    RequestStatus()
        : requestId(-1), state(RequestState::PENDING), code(ErrorCode::UNKNOWN),
          createdAt(0), startedAt(0), completedAt(0), retryCount(0) {
        memset(tag, 0, sizeof(tag));
        memset(prompt, 0, sizeof(prompt));
        memset(result, 0, sizeof(result));
    }

    unsigned long getElapsedTime() const {
        unsigned long endTime = (completedAt > 0) ? completedAt : millis();
        return (startedAt > 0) ? (endTime - startedAt) : 0;
    }

    bool isComplete() const {
        return state == RequestState::SUCCESS || 
               state == RequestState::FAILED ||
               state == RequestState::TIMEOUT ||
               state == RequestState::CANCELLED;
    }

    const char* stateToString() const {
        switch (state) {
            case RequestState::PENDING: return "PENDING";
            case RequestState::IN_PROGRESS: return "IN_PROGRESS";
            case RequestState::SUCCESS: return "SUCCESS";
            case RequestState::FAILED: return "FAILED";
            case RequestState::TIMEOUT: return "TIMEOUT";
            case RequestState::CANCELLED: return "CANCELLED";
        }
        return "UNKNOWN";
    }
};

class RequestTracker {
private:
    static RequestTracker* _instance;
    static int _nextRequestId;
    
    RequestStatus _requests[MAX_TRACKED_REQUESTS];
    uint8_t _requestCount = 0;
    
    std::function<void(const RequestStatus&)> _stateChangeCallback;

    RequestTracker() {}

public:
    static RequestTracker& getInstance() {
        if (!_instance) {
            _instance = new RequestTracker();
        }
        return *_instance;
    }

    int createRequest(const char* tag, const char* prompt) {
        if (_requestCount >= MAX_TRACKED_REQUESTS) {
            // Remove oldest completed request
            for (uint8_t i = 0; i < _requestCount; i++) {
                if (_requests[i].isComplete()) {
                    // Shift remaining
                    for (uint8_t j = i; j < _requestCount - 1; j++) {
                        _requests[j] = _requests[j + 1];
                    }
                    _requestCount--;
                    break;
                }
            }
            if (_requestCount >= MAX_TRACKED_REQUESTS) {
                return -1;
            }
        }

        int id = _nextRequestId++;
        RequestStatus& status = _requests[_requestCount++];
        
        status.requestId = id;
        status.createdAt = millis();
        status.state = RequestState::PENDING;
        
        strncpy(status.tag, tag, sizeof(status.tag) - 1);
        strncpy(status.prompt, prompt, sizeof(status.prompt) - 1);

        return id;
    }

    bool updateState(int requestId, RequestState newState, ErrorCode code = ErrorCode::SUCCESS) {
        for (uint8_t i = 0; i < _requestCount; i++) {
            if (_requests[i].requestId == requestId) {
                RequestStatus& req = _requests[i];
                req.state = newState;
                req.code = code;

                if (newState == RequestState::IN_PROGRESS && req.startedAt == 0) {
                    req.startedAt = millis();
                }

                if (req.isComplete() && req.completedAt == 0) {
                    req.completedAt = millis();
                }

                _notifyStateChange(req);
                return true;
            }
        }
        return false;
    }

    bool setResult(int requestId, const char* result) {
        for (uint8_t i = 0; i < _requestCount; i++) {
            if (_requests[i].requestId == requestId) {
                strncpy(_requests[i].result, result, sizeof(_requests[i].result) - 1);
                return true;
            }
        }
        return false;
    }

    bool incrementRetry(int requestId) {
        for (uint8_t i = 0; i < _requestCount; i++) {
            if (_requests[i].requestId == requestId) {
                _requests[i].retryCount++;
                return true;
            }
        }
        return false;
    }

    RequestStatus* getStatus(int requestId) {
        for (uint8_t i = 0; i < _requestCount; i++) {
            if (_requests[i].requestId == requestId) {
                return &_requests[i];
            }
        }
        return nullptr;
    }

    void setStateChangeCallback(std::function<void(const RequestStatus&)> callback) {
        _stateChangeCallback = callback;
    }

    int getActiveRequestCount() const {
        int count = 0;
        for (uint8_t i = 0; i < _requestCount; i++) {
            if (!_requests[i].isComplete()) count++;
        }
        return count;
    }

    void clearCompleted() {
        uint8_t writeIdx = 0;
        for (uint8_t i = 0; i < _requestCount; i++) {
            if (!_requests[i].isComplete()) {
                _requests[writeIdx++] = _requests[i];
            }
        }
        _requestCount = writeIdx;
    }

    void clearAll() {
        _requestCount = 0;
    }

    const RequestStatus* getAllRequests(uint8_t& count) const {
        count = _requestCount;
        return _requests;
    }

    uint8_t getRequestCount() const {
        return _requestCount;
    }

private:
    void _notifyStateChange(const RequestStatus& status) {
        if (_stateChangeCallback) {
            _stateChangeCallback(status);
        }
    }
};

RequestTracker* RequestTracker::_instance = nullptr;
int RequestTracker::_nextRequestId = 1000;
