#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <functional>
#include <cstring>
#include "MCUConfig.h"
#include "WatchdogHelper.h"

/**
 * Asynchronous HTTP Client Wrapper
 * 
 * Non-blocking HTTP client with state machine pattern
 * Replaces blocking xTaskCreatePinnedToCore with event-driven architecture
 * 
 * States:
 * - IDLE: Ready for request
 * - CONNECTING: Establishing connection
 * - SENDING: Transmitting request
 * - RECEIVING: Collecting response
 * - DONE: Complete (success or timeout)
 */

enum class AsyncHTTPState {
    IDLE,
    CONNECTING,
    SENDING,
    RECEIVING,
    DONE
};

struct AsyncHTTPResponse {
    static const size_t BUFFER_SIZE = 2048;
    
    int statusCode = 0;
    char body[BUFFER_SIZE] = {0};
    size_t bodyLength = 0;
    bool success = false;
    bool timeout = false;
};

typedef std::function<void(const AsyncHTTPResponse&)> FullResponseCallback;
typedef std::function<void(const char* chunk, size_t len)> ChunkCallback;
typedef std::function<void()> StreamCompleteCallback;


class AsyncHTTPClient {
private:
    AsyncHTTPState _state = AsyncHTTPState::IDLE;
    WiFiClientSecure _client;
    HTTPClient _http;
    
    String _url;
    String _method;  // "GET", "POST", "PUT", etc.
    String _payload;
    AsyncHTTPResponse _response;
    
    unsigned long _startTime = 0;
    unsigned long _timeout = HTTP_TIMEOUT_MS;
    
    FullResponseCallback _onComplete;

    // For streaming
    bool _isStreaming = false;
    ChunkCallback _onChunk;
    StreamCompleteCallback _onStreamComplete;
    
    char _authHeader[128];  // For Authorization header
    
public:
    AsyncHTTPClient() {
        _client.setInsecure();  // TODO: Add certificate pinning
        memset(_authHeader, 0, sizeof(_authHeader));
    }

    /**
     * Initialize an async HTTP GET request
     */
    void get(const String& url, FullResponseCallback onComplete) {
        _url = url;
        _method = "GET";
        _payload = "";
        _onComplete = onComplete;
        _state = AsyncHTTPState::CONNECTING;
        _startTime = millis();
    }

    /**
     * Initialize an async HTTP POST request
     */
    void post(const String& url, const String& payload, FullResponseCallback onComplete) {
        _url = url;
        _method = "POST";
        _payload = payload;
        _onComplete = onComplete;
        _isStreaming = false;
        _state = AsyncHTTPState::CONNECTING;
        _startTime = millis();
    }

    /**
     * Initialize an async HTTP POST request for streaming
     */
    void postStream(const String& url, const String& payload, ChunkCallback onChunk, StreamCompleteCallback onComplete) {
        _url = url;
        _method = "POST";
        _payload = payload;
        _onChunk = onChunk;
        _onStreamComplete = onComplete;
        _isStreaming = true;
        _state = AsyncHTTPState::CONNECTING;
        _startTime = millis();
    }

    /**
     * Set Authorization header (Bearer token)
     */
    void setAuthorization(const String& apiKey) {
        snprintf(_authHeader, sizeof(_authHeader), "Bearer %s", apiKey.c_str());
    }

    /**
     * Set request timeout
     */
    void setTimeout(unsigned long ms) {
        _timeout = ms;
    }

    /**
     * Process the async HTTP request
     * Should be called regularly in loop()
     * Returns true when complete, false if still processing
     */
    bool process() {
        WatchdogHelper::feed();  // Feed watchdog during async operation
        
        unsigned long elapsed = millis() - _startTime;
        
        // Check timeout
        if (elapsed > _timeout) {
            _response.timeout = true;
            _response.success = false;
            _state = AsyncHTTPState::DONE;
            _invokeCallback();
            return true;
        }

        switch (_state) {
            case AsyncHTTPState::CONNECTING: {
                if (_http.begin(_client, _url)) {
                    _http.setTimeout(_timeout);
                    _http.addHeader("Content-Type", "application/json");
                    
                    // Add Authorization header if set
                    if (strlen(_authHeader) > 0) {
                        _http.addHeader("Authorization", _authHeader);
                    }
                    
                    _state = AsyncHTTPState::SENDING;
                } else {
                    _response.success = false;
                    _state = AsyncHTTPState::DONE;
                    _invokeCallback();
                    return true;
                }
                break;
            }

            case AsyncHTTPState::SENDING: {
                WatchdogHelper::feed();  // Feed watchdog during HTTP send
                
                int code;
                if (_method == "POST") {
                    code = _http.POST(_payload);
                } else {
                    code = _http.GET();
                }
                
                _response.statusCode = code;
                
                if (code > 0) {
                    _state = AsyncHTTPState::RECEIVING;
                } else {
                    _response.success = false;
                    _state = AsyncHTTPState::DONE;
                    _invokeCallback();
                    return true;
                }
                break;
            }

            case AsyncHTTPState::RECEIVING: {
                WatchdogHelper::feed(); // Feed watchdog during HTTP receive

                if (_isStreaming) {
                    WiFiClient* stream = _http.getStreamPtr();
                    if (stream && stream->available()) {
                        char buffer[128];
                        size_t len = stream->readBytes(buffer, sizeof(buffer) - 1);
                        if (len > 0) {
                            buffer[len] = '\0';
                            if (_onChunk) {
                                _onChunk(buffer, len);
                            }
                        }
                    } else if (!stream || !stream->connected()) {
                        // Stream is finished
                        _http.end();
                        _state = AsyncHTTPState::DONE;
                        if (_onStreamComplete) _onStreamComplete();
                        return true;
                    }
                } else {
                    // Non-streaming response
                    String response = _http.getString();
                    _response.bodyLength = response.length();

                    if (_response.bodyLength < AsyncHTTPResponse::BUFFER_SIZE) {
                        strncpy(_response.body, response.c_str(),
                               AsyncHTTPResponse::BUFFER_SIZE - 1);
                        _response.body[AsyncHTTPResponse::BUFFER_SIZE - 1] = '\0';
                        _response.success = (_response.statusCode == 200);
                    } else {
                        _response.success = false; // Response too large
                    }

                    _http.end();
                    _state = AsyncHTTPState::DONE;
                    _invokeCallback();
                    return true;
                }
                break;
            }

            case AsyncHTTPState::DONE:
                return true;

            default:
                return false;
        }

        return false;  // Still processing
    }

    /**
     * Check if request is complete
     */
    bool isComplete() const {
        return _state == AsyncHTTPState::DONE;
    }

    /**
     * Get current state
     */
    AsyncHTTPState getState() const {
        return _state;
    }

    /**
     * Reset for new request
     */
    void reset() {
        _state = AsyncHTTPState::IDLE;
        _http.end();
        memset(&_response, 0, sizeof(_response));
        memset(_authHeader, 0, sizeof(_authHeader));
        _url = "";
        _isStreaming = false;
        _onChunk = nullptr;
        _onStreamComplete = nullptr;
        _method = "";
        _payload = "";
    }

    /**
     * Get response
     */
    const AsyncHTTPResponse& getResponse() const {
        return _response;
    }

private:
    void _invokeCallback() {
        if (_onComplete) {
            _onComplete(_response);
        }
    }
};

/**
 * Legacy wrapper for backward compatibility
 * Spawns request in a background task
 */
class AsyncHttpClient {
public:
    static void post(String url,
                     String payload,
                     String apiKey,
                     std::function<void(String)> callback) {

        xTaskCreatePinnedToCore(
            [](void* param) {

                auto* args = (std::tuple<String,String,String,
                              std::function<void(String)>>*)param;

                String url = std::get<0>(*args);
                String payload = std::get<1>(*args);
                String apiKey = std::get<2>(*args);
                auto callback = std::get<3>(*args);

                WiFiClientSecure client;
                client.setInsecure();

                HTTPClient https;
                https.begin(client, url);
                https.addHeader("Content-Type", "application/json");
                https.addHeader("Authorization", "Bearer " + apiKey);

                int code = https.POST(payload);
                String response = https.getString();

                https.end();

                callback(response);

                delete args;
                vTaskDelete(NULL);

            },
            "ArduClawHTTP",
            PROVIDER_STACK_SIZE,  // Use MCU-specific stack size
            new std::tuple<String,String,String,
                std::function<void(String)>>(
                url, payload, apiKey, callback),
            1,
            NULL,
            1
        );
    }
};
