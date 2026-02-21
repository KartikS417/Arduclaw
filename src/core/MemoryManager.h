#pragma once
#include <Arduino.h>

class MemoryManager {
private:
    static unsigned long _peakFreeHeap;
    static unsigned long _peakPsramFree;

public:
    static void begin() {
        updateMetrics();
    }

    static void updateMetrics() {
        unsigned long freeHeap = ESP.getFreeHeap();
        if (freeHeap > 0 && freeHeap < _peakFreeHeap) {
            _peakFreeHeap = freeHeap;
        }

        #ifdef ESP32
        unsigned long freePsram = ESP.getFreePsram();
        if (freePsram > 0 && freePsram < _peakPsramFree) {
            _peakPsramFree = freePsram;
        }
        #endif
    }

    static unsigned long getFreeHeap() {
        return ESP.getFreeHeap();
    }

    static unsigned long getPeakFreeHeap() {
        return _peakFreeHeap;
    }

    static unsigned long getFreePsram() {
        #ifdef ESP32
        return ESP.getFreePsram();
        #else
        return 0;
        #endif
    }

    static unsigned long getPeakFreePsram() {
        return _peakPsramFree;
    }

    static String getStatus() {
        String status = "Heap: " + String(getFreeHeap()) + " bytes";
        #ifdef ESP32
        status += " | PSRAM: " + String(getFreePsram()) + " bytes";
        #endif
        return status;
    }

    static bool lowMemory(unsigned long threshold = 200000) {
        return ESP.getFreeHeap() < threshold;
    }

    static bool isMemoryLow(unsigned long threshold = 50000) {
        return getFreeHeap() < threshold;
    }

    static int recommendedTokens() {
        if (lowMemory()) return 100;
        return 300;
    }
};

unsigned long MemoryManager::_peakFreeHeap = UINT32_MAX;
unsigned long MemoryManager::_peakPsramFree = UINT32_MAX;
