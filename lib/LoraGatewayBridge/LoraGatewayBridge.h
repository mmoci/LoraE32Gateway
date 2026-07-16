#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include "MqttClient.h"
#include "E32LoraModule.h"

/**
 * @class LoraGatewayBridge
 * @brief Domain bridge: translates between raw E32 LoRa bytes and MQTT messages.
 *
 * ── Message protocol ────────────────────────────────────────────────────────
 * Every LoRa message sent by a sensor node must start with a 1-byte node ID:
 *
 *   Byte 0    : Node ID  (matches a registerNode() call below)
 *   Byte 1..N : Payload  (sensor-specific; passed to the NodeDef::decode function)
 *
 * The node ID lets the gateway route and decode each message independently.
 * Messages from unregistered node IDs fall back to raw hex on the generic rx topic.
 *
 * ── Topic structure ──────────────────────────────────────────────────────────
 * All topics are relative to  {topicPrefix}{deviceId}/:
 *
 *   availability              — "online" / "offline" (LWT)
 *   {nodeName}/state          — decoded state for a registered sensor node
 *   rx                        — raw hex fallback for unregistered nodes
 *   tx/set                    — outbound MQTT → LoRa (hex string payload)
 *
 * ── HomeAssistant discovery ──────────────────────────────────────────────────
 *   homeassistant/{component}/{deviceId}/{nodeName}/config
 *
 * ── Usage ────────────────────────────────────────────────────────────────────
 *   LoraGatewayBridge bridge{mqttClient, loraModule, {DEVICE_ID, TOPIC_PREFIX}};
 *   bridge.registerNode(0x01, {"mailbox",  "binary_sensor", "", "occupancy",
 *       [](const uint8_t* d, size_t) { return d[0]==0x55 ? "ON" : "OFF"; }});
 *   bridge.registerNode(0x02, {"humidity", "sensor", "%", "humidity",
 *       [](const uint8_t* d, size_t) { return std::to_string(d[0]); }});
 *   bridge.init();
 *   bridge.process();
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

    /**
     * @brief Describes a registered sensor node.
     *
     * @param name        Short identifier used in MQTT topics, e.g. "mailbox".
     *                    Results in topic:  lora/{deviceId}/{name}/state
     * @param haComponent HomeAssistant component type: "binary_sensor", "sensor", etc.
     * @param unit        Unit of measurement shown in HA (empty string if not applicable).
     * @param deviceClass HA device class, e.g. "occupancy", "humidity", "temperature".
     *                    See https://www.home-assistant.io/integrations/sensor/#device-class
     * @param decode      Converts raw payload bytes (everything after the node-ID byte)
     *                    into the state string published to MQTT.
     * @param ackByte     If non-zero, this byte is sent back to the sensor over LoRa
     *                    immediately after a message is received. Use this for sensors
     *                    that implement an acknowledgement/retransmission protocol
     *                    (e.g. 0x25 for the mailbox sensor).
     */
    struct NodeDef
    {
        std::string name;
        std::string haComponent;
        std::string unit;
        std::string deviceClass;
        std::function<std::string(const uint8_t* payload, size_t len)> decode;
        uint8_t ackByte{0};  ///< ACK byte sent back to sensor (0 = no ACK)
    };

    LoraGatewayBridge(MqttClient& mqttClient, E32LoraModule& loraModule, const Config& config);

    /**
     * @brief Register a sensor node that this gateway will receive messages from.
     *
     * @param nodeId  First byte of every LoRa message sent by this node.
     * @param def     Node description: name, HA component type, decoder function.
     *
     * Call before init() so HA discovery is published on the first connection.
     */
    void registerNode(uint8_t nodeId, NodeDef def);

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
    void publishNodeDiscovery(const NodeDef& def);
    void onLoraReceived(const uint8_t* data, size_t len);
    void onMqttTx(std::string_view payload);

    static std::string toHexString(const uint8_t* data, size_t len);
    static bool fromHexString(std::string_view hex, uint8_t* out, size_t& outLen);

    MqttClient&    m_mqttClient;
    E32LoraModule& m_loraModule;
    Config         m_config;

    std::unordered_map<uint8_t, NodeDef> m_nodes; ///< registered sensor nodes keyed by node ID

    std::string m_rxTopic;
    std::string m_txTopic;
    std::string m_availabilityTopic;
};
