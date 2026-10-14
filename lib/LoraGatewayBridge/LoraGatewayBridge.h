#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <vector>
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
 *   Byte 1    : Entity ID (matches a NodeEntityConfig.entityId for that node)
 *   Byte 2..N : Payload  (sensor-specific; passed to the NodeConfig::decode function)
 *
 * The node ID lets the gateway route and decode each message independently.
 * Messages from unregistered node IDs fall back to raw hex on the generic rx topic.
 *
 * ── Topic structure ──────────────────────────────────────────────────────────
 * All topics are relative to  {topicPrefix}{deviceId}/:
 *
 *   availability                    — "online" / "offline" (LWT)
 *   {entityName}/state              — decoded state per registered entity
 *   {entityName}/set                — inbound command per entity (writable entities only)
 *   rx                              - raw hex fallback for unregistered nodes
 *
 * ── HomeAssistant discovery ──────────────────────────────────────────────────
 *   homeassistant/{component}/{proxyName}_{deviceName}/{entityName}/config
 *
 * ── Usage ────────────────────────────────────────────────────────────────────
 *   LoraGatewayBridge bridge{mqttClient, loraModule, {PROXY_NAME, TOPIC_PREFIX}};
 *   bridge.registerNode(0x01, {
 *       .name     = "mailbox",
 *       .entities = {{
 *           .entityId    = 0x01,
 *           .name        = "mailbox_door",
 *           .haComponent = "binary_sensor",
 *           .deviceClass = "occupancy",
 *           .decode      = [](const uint8_t* d, size_t) { return d[0]==0x55 ? "ON" : "OFF"; },
 *           .ackMessagePayload  = {0x25}
 *       }}
 *   });
 *   bridge.init();
 *   bridge.process();
 */
class LoraGatewayBridge
{
public:
    struct Config
    {
        std::string_view proxyName;   ///< Human-readable name for the proxy (e.g., "lora_gateway")
        std::string_view topicPrefix; ///< e.g. "lora/"
    };

    struct NodeEntityConfig
    {
        uint8_t     entityId;       ///< Unique ID for this entity within the node
        std::string name;           ///< MQTT topic segment — lowercase, no spaces (e.g. "mailbox")
        std::string friendlyName;   ///< HA display name — can have capitals/spaces (e.g. "Mailbox"); defaults to name if empty
        std::string haComponent;
        std::string unit;
        std::string deviceClass;
        std::function<std::string(const uint8_t* payload, size_t len)> decode;
        std::function<std::vector<uint8_t>(std::string_view payload)> encode;
        std::vector<uint8_t> ackMessagePayload{};

        std::string getHaDiscoveryTopic(std::string_view proxyName, std::string_view deviceName) const;
        std::string getStateTopic(std::string_view topicPrefix, std::string_view proxyName, std::string_view deviceName) const;
        std::string getCommandTopic(std::string_view topicPrefix, std::string_view proxyName, std::string_view deviceName) const;
    };

    struct NodeConfig
    {
        std::string deviceName;                  ///< MQTT topic segment — lowercase, no spaces (e.g. "mailbox")
        std::string friendlyDeviceName;          ///< HA display name — can have capitals/spaces (e.g. "Mailbox"); defaults to deviceName if empty
        std::vector<NodeEntityConfig> entities;  ///< One or more entities (sensors, binary_sensors, etc.) for this node
    };

    LoraGatewayBridge(MqttClient& mqttClient, E32LoraModule& loraModule, const Config& config);

    /**
     * @brief Register a sensor node that this gateway will receive messages from.
     *
     * @param nodeId  First byte of every LoRa message sent by this node.
     * @param nodeConfig Node description: name, HA component type, decoder function.
     *
     * Call before init() so HA discovery is published on the first connection.
     */
    void registerNode(uint8_t nodeId, NodeConfig nodeConfig);

    /**
     * @brief Subscribes to MQTT command topics and publishes HA discovery messages.
     *        Leaves the E32 module in Normal mode.
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
    void publishNodeDiscovery(const NodeConfig& nodeConfig);
    void handleLoraMsg(const uint8_t* data, size_t len);
    void handleMqttMsg(uint8_t nodeId, uint8_t entityId, std::string_view payload);
    void subscribeCommandTopics();

    static std::string toHexString(const uint8_t* data, size_t len);
    static std::vector<uint8_t> fromHexString(std::string_view hex);

    MqttClient&    m_mqttClient;
    E32LoraModule& m_loraModule;
    Config         m_config;
    std::unordered_map<uint8_t, NodeConfig> m_nodes; ///< registered sensor nodes keyed by node ID
    std::string m_rxTopic;
    std::string m_availabilityTopic;
};
