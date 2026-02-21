#pragma once
#include <Arduino.h>

enum class ErrorCode {
    // Success codes
    SUCCESS = 0,
    
    // Provider errors
    PROVIDER_NOT_FOUND = 100,
    PROVIDER_BUSY = 101,
    PROVIDER_TIMEOUT = 102,
    PROVIDER_FAILED = 103,
    
    // Network errors
    NETWORK_ERROR = 200,
    HTTP_ERROR = 201,
    HTTP_TIMEOUT = 202,
    CONNECTION_REFUSED = 203,
    DNS_FAILED = 204,
    
    // Request errors
    INVALID_REQUEST = 300,
    INVALID_RESPONSE = 301,
    RATE_LIMIT_EXCEEDED = 302,
    MAX_RETRIES_EXCEEDED = 303,
    
    // JSON/Parsing errors
    JSON_PARSE_ERROR = 400,
    JSON_VALIDATION_ERROR = 401,
    MISSING_FIELD = 402,
    
    // Configuration errors
    CONFIG_INVALID = 500,
    CONFIG_NOT_LOADED = 501,
    
    // Memory errors
    MEMORY_INSUFFICIENT = 600,
    ALLOCATION_FAILED = 601,
    
    // Channel errors
    CHANNEL_NOT_FOUND = 700,
    CHANNEL_UNAVAILABLE = 701,
    
    // Unknown error
    UNKNOWN = 999
};

class ErrorCodeHelper {
public:
    static const char* toString(ErrorCode code) {
        switch (code) {
            case ErrorCode::SUCCESS: return "Success";
            case ErrorCode::PROVIDER_NOT_FOUND: return "Provider not found";
            case ErrorCode::PROVIDER_BUSY: return "Provider busy";
            case ErrorCode::PROVIDER_TIMEOUT: return "Provider timeout";
            case ErrorCode::PROVIDER_FAILED: return "Provider failed";
            case ErrorCode::NETWORK_ERROR: return "Network error";
            case ErrorCode::HTTP_ERROR: return "HTTP error";
            case ErrorCode::HTTP_TIMEOUT: return "HTTP timeout";
            case ErrorCode::CONNECTION_REFUSED: return "Connection refused";
            case ErrorCode::DNS_FAILED: return "DNS failed";
            case ErrorCode::INVALID_REQUEST: return "Invalid request";
            case ErrorCode::INVALID_RESPONSE: return "Invalid response";
            case ErrorCode::RATE_LIMIT_EXCEEDED: return "Rate limit exceeded";
            case ErrorCode::MAX_RETRIES_EXCEEDED: return "Max retries exceeded";
            case ErrorCode::JSON_PARSE_ERROR: return "JSON parse error";
            case ErrorCode::JSON_VALIDATION_ERROR: return "JSON validation error";
            case ErrorCode::MISSING_FIELD: return "Missing field";
            case ErrorCode::CONFIG_INVALID: return "Config invalid";
            case ErrorCode::CONFIG_NOT_LOADED: return "Config not loaded";
            case ErrorCode::MEMORY_INSUFFICIENT: return "Insufficient memory";
            case ErrorCode::ALLOCATION_FAILED: return "Allocation failed";
            case ErrorCode::CHANNEL_NOT_FOUND: return "Channel not found";
            case ErrorCode::CHANNEL_UNAVAILABLE: return "Channel unavailable";
            case ErrorCode::UNKNOWN: 
            default: return "Unknown error";
        }
    }

    static bool isSuccess(ErrorCode code) {
        return code == ErrorCode::SUCCESS;
    }

    static bool isRetryable(ErrorCode code) {
        switch (code) {
            case ErrorCode::PROVIDER_TIMEOUT:
            case ErrorCode::HTTP_TIMEOUT:
            case ErrorCode::NETWORK_ERROR:
            case ErrorCode::CONNECTION_REFUSED:
            case ErrorCode::RATE_LIMIT_EXCEEDED:
                return true;
            default:
                return false;
        }
    }
};
