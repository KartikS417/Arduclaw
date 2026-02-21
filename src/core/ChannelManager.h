#pragma once
#include "../channels/BaseChannel.h"
#include "MCUConfig.h"

class ChannelManager {
private:
    BaseChannel* _channels[MAX_CHANNELS];
    uint8_t _channelCount = 0;

public:
    void add(BaseChannel* c) {
        if (c && _channelCount < MAX_CHANNELS) {
            _channels[_channelCount++] = c;
        }
    }

    void begin() {
        for (uint8_t i = 0; i < _channelCount; i++)
            _channels[i]->begin();
    }

    void loop() {
        for (uint8_t i = 0; i < _channelCount; i++)
            _channels[i]->loop();
    }

    BaseChannel** get() {
        return _channels;
    }

    uint8_t count() const {
        return _channelCount;
    }
};
