#include "OtaHandler.h"
#include <Arduino.h>
#include "Logger.h"

static constexpr char TAG[] = "OtaHandler";

void OtaHandler::init()
{
    ArduinoOTA.setHostname(std::string{m_config.hostname}.c_str());
    ArduinoOTA.setPassword(std::string{m_config.password}.c_str());

    ArduinoOTA.onStart([]() {
        ESP_LOGI(TAG, "OTA update starting...");
    });
    ArduinoOTA.onEnd([]() {
        ESP_LOGI(TAG, "OTA update complete — rebooting");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        ESP_LOGD(TAG, "Progress: %u%%", progress / (total / 100));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        const char* msg = "Unknown";
        switch (error) {
            case OTA_AUTH_ERROR:    msg = "Auth failed";     break;
            case OTA_BEGIN_ERROR:   msg = "Begin failed";    break;
            case OTA_CONNECT_ERROR: msg = "Connect failed";  break;
            case OTA_RECEIVE_ERROR: msg = "Receive failed";  break;
            case OTA_END_ERROR:     msg = "End failed";      break;
        }
        ESP_LOGE(TAG, "Error: %s", msg);
    });

    ArduinoOTA.begin();
    ESP_LOGI(TAG, "OTA ready — %s.local", std::string{m_config.hostname}.c_str());
}

void OtaHandler::handle()
{
    ArduinoOTA.handle();
}
