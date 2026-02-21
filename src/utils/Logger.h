#pragma once
#include <Arduino.h>

#define LOG_LEVEL_NONE   0
#define LOG_LEVEL_ERROR  1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_INFO   3
#define LOG_LEVEL_DEBUG  4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOGE(tag, msg) if (LOG_LEVEL >= LOG_LEVEL_ERROR) Serial.printf("[ERROR][%s] %s\n", tag, msg)
#define LOGW(tag, msg) if (LOG_LEVEL >= LOG_LEVEL_WARN)  Serial.printf("[WARN ][%s] %s\n", tag, msg)
#define LOGI(tag, msg) if (LOG_LEVEL >= LOG_LEVEL_INFO)  Serial.printf("[INFO ][%s] %s\n", tag, msg)
#define LOGD(tag, msg) if (LOG_LEVEL >= LOG_LEVEL_DEBUG) Serial.printf("[DEBUG][%s] %s\n", tag, msg)