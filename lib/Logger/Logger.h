#pragma once

#include <functional>
#include <string>
#include <cstdarg>

/**
 * @file Logger.h
 * @brief Portable logging shim.
 *
 * On ESP32: delegates to ESP-IDF esp_log — runtime per-module level control:
 *   esp_log_level_set("E32LoraModule", ESP_LOG_DEBUG)  // one module verbose
 *   esp_log_level_set("*", ESP_LOG_WARN)               // silence everything
 *
 * On generic Arduino (non-ESP32): maps to Serial.printf.
 * On native test target: ERROR to stderr, rest suppressed.
 *
 * Usage:
 *   #include "Logger.h"
 *   static const char* TAG = "MyModule";
 *
 *   ESP_LOGI(TAG, "Connected. IP: %s", ip);
 *   ESP_LOGW(TAG, "Retrying...");
 *   ESP_LOGE(TAG, "Fatal error: %d", err);
 *   ESP_LOGD(TAG, "Rx byte: 0x%02X", b);
 */

using LogHandlerFn = std::function<void(const std::string& level, const std::string& tag, const std::string& message)>;

#if defined(ESP32) || defined(ESP_PLATFORM)
    #include <esp_log.h>

    #ifdef ESP_LOGE
        #undef ESP_LOGE
    #endif
    #ifdef ESP_LOGI
        #undef ESP_LOGI
    #endif
    #ifdef ESP_LOGW
        #undef ESP_LOGW
    #endif
    #ifdef ESP_LOGD
        #undef ESP_LOGD
    #endif

    inline const char* _log_fmt_time(char* buf, unsigned long ms)
    {
        unsigned long s = ms / 1000;
        unsigned long m = s  / 60;
        unsigned long h = m  / 60;
        snprintf(buf, 16, "%02lu:%02lu:%02lu.%03lu", h, m % 60, s % 60, ms % 1000);
        return buf;
    }
    #define _LOG_TS() ([]() -> const char* { static char _b[16]; return _log_fmt_time(_b, (unsigned long)esp_log_timestamp()); }())

    #define ESP_LOGE(tag, fmt, ...) do { \
        esp_log_write(ESP_LOG_ERROR, tag, "[ERROR] [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__); \
        Logger::_dispatch("ERROR", tag, fmt, ##__VA_ARGS__); \
    } while(0)
    #define ESP_LOGI(tag, fmt, ...) do { \
        esp_log_write(ESP_LOG_INFO,  tag, "[INFO]  [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__); \
        Logger::_dispatch("INFO",  tag, fmt, ##__VA_ARGS__); \
    } while(0)
    #define ESP_LOGW(tag, fmt, ...) do { \
        esp_log_write(ESP_LOG_WARN,  tag, "[WARN]  [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__); \
        Logger::_dispatch("WARN",  tag, fmt, ##__VA_ARGS__); \
    } while(0)
    #define ESP_LOGD(tag, fmt, ...) do { \
        esp_log_write(ESP_LOG_DEBUG, tag, "[DEBUG] [%s] %s: " fmt "\n", _LOG_TS(), tag, ##__VA_ARGS__); \
        Logger::_dispatch("DEBUG", tag, fmt, ##__VA_ARGS__); \
    } while(0)

#elif defined(ARDUINO)
    #include <Arduino.h>
    #define ESP_LOGE(tag, fmt, ...) do { Serial.printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__);   Logger::_dispatch("ERROR", tag, fmt, ##__VA_ARGS__); } while(0)
    #define ESP_LOGW(tag, fmt, ...) do { Serial.printf("[WARNING][%s] " fmt "\n", tag, ##__VA_ARGS__); Logger::_dispatch("WARN",  tag, fmt, ##__VA_ARGS__); } while(0)
    #define ESP_LOGI(tag, fmt, ...) do { Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__);    Logger::_dispatch("INFO",  tag, fmt, ##__VA_ARGS__); } while(0)
    #define ESP_LOGD(tag, fmt, ...) do { Serial.printf("[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__);   Logger::_dispatch("DEBUG", tag, fmt, ##__VA_ARGS__); } while(0)

#else
    #include <cstdio>
    #define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
    #define ESP_LOGW(tag, fmt, ...) do {} while(0)
    #define ESP_LOGI(tag, fmt, ...) do {} while(0)
    #define ESP_LOGD(tag, fmt, ...) do {} while(0)

#endif

namespace Logger
{
    inline LogHandlerFn _mqttLogHandler{};

    inline bool mqttDebugEnabled{false};

    inline void setMqttLogHandler(LogHandlerFn handler)
    {
        _mqttLogHandler = std::move(handler);
    }

    inline void clearLogHandler()
    {
        _mqttLogHandler = nullptr;
    }

    inline void _dispatch(const char* level, const char* tag, const char* fmt, ...)
    {
        if (!_mqttLogHandler) return;

        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        if (mqttDebugEnabled || strcmp(level, "DEBUG") != 0)
            _mqttLogHandler(level, tag, buf);
    }
}
