#pragma once
#include <Arduino.h>

class BaseChannel {
public:
    virtual void begin() = 0;
    virtual void loop() = 0;
    virtual bool available() = 0;
    virtual String readMessage() = 0;
    virtual void sendMessage(const String& msg) = 0;
};
