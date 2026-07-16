#pragma once

#include <string>
#include <string_view>
#include "MqttClient.h"
#include "E32LoraModule.h"

/**
 * @class LoraGatewayBridge
 * @brief Domain bridge: translates between raw E32 LoRa bytes and MQTT messages.
 *
 * Topic structure (all relative to  {topicPrefix}{deviceId}/):
 *
 *   availability          — "online" / "offline" (LWT, managed by MqttClient)
 *   rx                    — inbound LoRa → MQTT, JSON: {"hex":"AABB","len":2}
 *   tx/set                — outbound MQTT → LoRa; payload = hex string, e.g. "AABB"
 *
 * HomeAssistant discovery:
 *   homeassistant/sensor/{deviceId}/rx/config
 *
 * Usage:
 *   LoraGatewayBridge bridge{mqttClient, loraModule, {DEVICE_ID, TOPIC_PREFIX}};
 *   bridge.init();      // call once from setup()
 *   bridge.process();   // call every loop()
 */
class LoraGatewayBridge
{
public:
    struct Config
    {
        std::string_view deviceId;
        std::string_view topicPrefix; ///< e.g. "lora/"
        std::string_view deviceName;  ///< Human-readable name for HA device registry
    };

    LoraGatewayBridge(MqttClient& mqttClient, E32LoraModule& loraModule, const Config& config);

    /**
     * @brief Subscribes to MQTT TX topic and registers HA discovery publish on connect.
     *        Call once from setup(), after MqttClient::init().
     */
    void init();

    /**
     * @brief Polls the E32 module for incoming bytes and forwards them to MQTT.
     *        Call every loop() iteration.
     */
    void process();

private:
    void publishHaDiscovery();
    void onLoraReceived(const uint8_t* data, size_t len);
    void onMqttTx(std::string_view payload);

    /// Convert a byte array to an uppercase hex string (e.g. {0xAB, 0x0F} → "AB0F")
    static std::string toHexString(const uint8_t* data, size_t len);

    /// Parse an even-length hex string into bytes. Returns false on invalid input.
    static bool fromHexString(std::string_view hex, uint8_t* out, size_t& outLen);

    MqttClient&    m_mqttClient;
    E32LoraModule& m_loraModule;
    Config         m_config;

    std::string m_rxTopic;
    std::string m_txTopic;
    std::string m_availabilityTopic;
};
