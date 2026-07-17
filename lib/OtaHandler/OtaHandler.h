#pragma once

#include <ArduinoOTA.h>
#include <string_view>
#include <string>

/**
 * @class OtaHandler
 * @brief Thin wrapper around ArduinoOTA for over-the-air firmware updates.
 *
 * The device appears on the network as {hostname}.local (mDNS).
 * Upload from PlatformIO: add  upload_protocol = espota  and
 * upload_port = lora-gateway.local  to the env in platformio.ini, or
 * trigger from Arduino IDE via Tools → Port → Network ports.
 *
 * Requires WiFi to already be connected (MqttClient::init() must run first).
 */
class OtaHandler
{
public:
    struct Config
    {
        std::string_view hostname; ///< mDNS name — device appears as {hostname}.local
        std::string_view password; ///< Password required by OTA client
    };

    explicit OtaHandler(const Config& config) : m_config{config} {}

    /**
     * @brief Register ArduinoOTA callbacks and start the OTA service.
     *        Call once from setup(), after WiFi is connected.
     */
    void init();

    /**
     * @brief Drive the OTA state machine.
     *        Call every loop() iteration.
     */
    void handle();

private:
    Config m_config;
};
