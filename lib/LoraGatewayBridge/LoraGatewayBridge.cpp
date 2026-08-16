#include "LoraGatewayBridge.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cctype>

static constexpr char TAG[] = "LoraGatewayBridge";

static constexpr size_t RX_BUFFER_SIZE {64};

LoraGatewayBridge::LoraGatewayBridge(MqttClient& mqttClient, E32LoraModule& loraModule, const Config& config)
    : m_mqttClient{mqttClient}
    , m_loraModule{loraModule}
    , m_config{config}
{
    std::string base{m_config.topicPrefix};
    base.append(m_config.deviceId);
    base.append("/");

    m_availabilityTopic = base + "availability";
    m_rxTopic           = base + "rx";
    m_txTopic           = base + "tx/set";
}

void LoraGatewayBridge::registerNode(uint8_t nodeId, NodeConfig nodeConfig)
{
    ESP_LOGI(TAG, "Node 0x%02X registered: %s", nodeId, nodeConfig.name.c_str());
    
    for (const auto& nodeEntity : nodeConfig.entities)
    {
        ESP_LOGI(TAG, "  Entity: 0x%02X | %s (%s) | HA component: %s | unit: %s | device_class: %s",
            nodeEntity.entityId,
            nodeEntity.name.c_str(),
            nodeEntity.friendlyName.empty() ? "<no friendly name>" : nodeEntity.friendlyName.c_str(),
            nodeEntity.haComponent.c_str(),
            nodeEntity.unit.empty() ? "<no unit>" : nodeEntity.unit.c_str(),
            nodeEntity.deviceClass.empty() ? "<no device class>" : nodeEntity.deviceClass.c_str());
    }
    m_nodes.emplace(nodeId, std::move(nodeConfig));
}

void LoraGatewayBridge::init()
{
    m_mqttClient.onConnect([this]() 
    { 
        subscribeCommandTopics();
        publishHaDiscovery(); 
    });

    ESP_LOGI(TAG, "Bridge initialised. RX fallback: %s | Nodes: %u",
        m_rxTopic.c_str(), (unsigned)m_nodes.size());
}

void LoraGatewayBridge::process()
{
    if (!m_loraModule.available())
        return;

    // At 9600 baud each byte takes ~1ms. Wait 10ms so all bytes of the
    // packet have arrived in the UART buffer before reading. Without this,
    // process() reads only byte 0 (node ID) and misses the payload byte.
    delay(10);

    uint8_t buf[RX_BUFFER_SIZE];
    size_t len = m_loraModule.read(buf, sizeof(buf));
    if (len > 0)
        handleLoraMsg(buf, len);
}


/******************
* Private methods *
*******************/

void LoraGatewayBridge::subscribeCommandTopics()
{
    for (const auto& [nodeId, nodeConfig] : m_nodes)
    {
        for (const auto& entity : nodeConfig.entities)
        {
            if(!entity.encode) continue;

            m_mqttClient.subscribe(entity.getCommandTopic(m_config.topicPrefix, m_config.deviceId), 
                [this, nodeId, entityId = entity.entityId](std::string_view payload) {
                    handleMqttMsg(nodeId, entityId, payload);
            });
        }
    }
}

void LoraGatewayBridge::publishHaDiscovery()
{
    for (const auto& [nodeId, nodeConfig] : m_nodes)
        publishNodeDiscovery(nodeConfig);
}

void LoraGatewayBridge::publishNodeDiscovery(const NodeConfig& nodeConfig)
{
    for (const auto& entity : nodeConfig.entities)
    {
        std::string discoveryTopic{entity.getHaDiscoveryTopic(m_config.deviceId)};
        std::string stateTopic{entity.getStateTopic(m_config.topicPrefix, m_config.deviceId)};

        JsonDocument doc{};

        doc["name"]                  = entity.friendlyName.empty() ? entity.name : entity.friendlyName;
        doc["unique_id"]             = std::string{m_config.deviceId} + "_" + entity.name;
        doc["state_topic"]           = stateTopic;
        doc["availability_topic"]    = m_availabilityTopic;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";

        if (!entity.unit.empty())
            doc["unit_of_measurement"] = entity.unit;
        if (!entity.deviceClass.empty())
            doc["device_class"] = entity.deviceClass;

        JsonObject device{ doc["device"].to<JsonObject>() };

        device["identifiers"][0] = m_config.deviceId;
        device["name"]           = m_config.deviceName;
        device["model"]          = "E32 LoRa Gateway";
        device["manufacturer"]   = "Custom";

        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(discoveryTopic, payload, /*retain=*/true);
        ESP_LOGD(TAG, "HA discovery: %s", discoveryTopic.c_str());
    }
}

void LoraGatewayBridge::handleLoraMsg(const uint8_t* data, size_t len)
{
    static uint32_t msgCount{0};
    ++msgCount;

    if (len < 2) return;

    // First byte is the node ID, second byte is the entity ID
    uint8_t nodeId = data[0];
    uint8_t entityId = data[1];

    auto it = m_nodes.find(nodeId);

    if (it != m_nodes.end())
    {
        const NodeConfig& nodeConfig{it->second};

        auto entityIt = std::find_if(nodeConfig.entities.begin(), nodeConfig.entities.end(),
            [entityId](const NodeEntityConfig& e) { return e.entityId == entityId; });

        if (entityIt == nodeConfig.entities.end())
        {
            ESP_LOGW(TAG, "Node 0x%02X: unknown entity ID 0x%02X — ignoring", nodeId, entityId);
            return;
        }

        if(!entityIt->decode)
        {
            ESP_LOGW(TAG, "Node 0x%02X: entity ID 0x%02X has no decoder — ignoring", nodeId, entityId);
            return;
        }

        const uint8_t* payload    = data + 2;
        size_t         payloadLen = len - 2;

        std::string state     {entityIt->decode(payload, payloadLen)};
        std::string stateTopic{entityIt->getStateTopic(m_config.topicPrefix, m_config.deviceId)};

        m_mqttClient.publish(stateTopic, state, /*retain=*/true);
        ESP_LOGI(TAG, "Node 0x%02X, entity 0x%02X (%s) → %s", nodeId, entityId, entityIt->name.c_str(), state.c_str());

        // Send ACK back to sensor if configured (before the sensor retransmits)
        if (!entityIt->ackMessagePayload.empty() && entityIt->ackMessagePayload[0] != 0)
        {
            std::vector<uint8_t> ackMessage{nodeId, entityId};
            ackMessage.insert(ackMessage.end(), entityIt->ackMessagePayload.begin(), entityIt->ackMessagePayload.end());
            m_loraModule.send(ackMessage.data(), ackMessage.size());
            ESP_LOGD(TAG, "Node 0x%02X ACK sent: %s", nodeId, 
                toHexString(entityIt->ackMessagePayload.data(), entityIt->ackMessagePayload.size()).c_str());
        }
    }
    else
    {
        // ── Unknown node: publish raw hex to the generic fallback topic ──────────
        std::string hexStr = toHexString(data, len);
        ESP_LOGW(TAG, "Unknown node 0x%02X — raw hex: %s", nodeId, hexStr.c_str());

        JsonDocument doc;
        doc["hex"]    = hexStr;
        doc["len"]    = len;
        doc["count"]  = msgCount;
        doc["nodeId"] = nodeId;

        std::string jsonPayload;
        serializeJson(doc, jsonPayload);
        m_mqttClient.publish(m_rxTopic, jsonPayload, /*retain=*/true);
    }
}

void LoraGatewayBridge::handleMqttMsg(uint8_t nodeId, uint8_t entityId, std::string_view payload)
{
    if (payload.empty())
    {
        ESP_LOGW(TAG, "MQTT: empty payload — ignoring");
        return;
    }

    auto it = m_nodes.find(nodeId);

    if (it == m_nodes.end())
    {
        ESP_LOGE(TAG, "MQTT: unknown node ID 0x%02X — ignoring", nodeId);
        return;
    }

    const NodeConfig& nodeConfig = it->second;

    auto entityIt = std::find_if(nodeConfig.entities.begin(), nodeConfig.entities.end(),
        [entityId](const NodeEntityConfig& e) { return e.entityId == entityId; });

    if (entityIt == nodeConfig.entities.end())
    {
        ESP_LOGE(TAG, "MQTT: unknown entity ID 0x%02X for nodeId 0x%02X — ignoring", entityId, nodeId);
        return;
    }

    if (!entityIt->encode) 
    { 
        ESP_LOGE(TAG, "MQTT: entity ID 0x%02X for nodeId 0x%02X has no encoder — ignoring", entityId, nodeId); 
        return; 
    }

    auto encodedPayload = entityIt->encode(payload);

    std::vector<uint8_t> buf{nodeId};
    buf.insert(buf.end(), encodedPayload.begin(), encodedPayload.end());

    if (buf.size() < 2)
    {
        ESP_LOGE(TAG, "MQTT: nodeId:0x%02X, entityId:0x%02X - failed to encode '%.*s' — ignoring", 
                nodeId, entityId, static_cast<int>(payload.size()), payload.data());
        return;
    }

    ESP_LOGI(TAG, "MQTT -> LoRa: nodeId:0x%02X, entityId:0x%02X (%u byte(s))", nodeId, entityId, (unsigned)buf.size());
    m_loraModule.send(buf.data(), buf.size());
}

std::string LoraGatewayBridge::toHexString(const uint8_t* data, size_t len)
{
    std::string out{};

    if(len == 0 || data == nullptr)
        return "";

    auto hexValue = [](const uint8_t byte) -> unsigned char {
        if (byte >= 0x0 && byte <= 0x9) return static_cast<unsigned char>(byte + '0');
        if (byte >= 0xA && byte <= 0xF) return static_cast<unsigned char>(byte + 'A' - 10);
        return 0;
    };

    for (size_t i = 0; i < len; ++i)
    {
        out.push_back(hexValue((data[i] >> 4) & 0x0F));
        out.push_back(hexValue(data[i] & 0x0F));
    }
    return out;
}

std::vector<uint8_t> LoraGatewayBridge::fromHexString(std::string_view hex)
{
    std::vector<uint8_t> out{};

    if (hex.size() % 2 != 0)
        return out;

    for (size_t i = 0; i < hex.size(); i += 2)
    {
        char hi = hex[i];
        char lo = hex[i + 1];

        if (!isxdigit(static_cast<unsigned char>(hi)) || !isxdigit(static_cast<unsigned char>(lo)))
            return {};

        auto hexVal = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            return static_cast<uint8_t>(c - 'A' + 10);
        };

        out.push_back(static_cast<uint8_t>((hexVal(hi) << 4) | hexVal(lo)));
    }
    return out;
}

std::string LoraGatewayBridge::NodeEntityConfig::getHaDiscoveryTopic(std::string_view deviceId) const
{
    std::string topic{"homeassistant/"};
    topic.append(haComponent).append("/").append(deviceId).append("/").append(name).append("/config");
    return topic;
}

std::string LoraGatewayBridge::NodeEntityConfig::getStateTopic(std::string_view topicPrefix, std::string_view deviceId) const
{
    std::string topic{topicPrefix};
    topic.append(deviceId).append("/").append(name).append("/state");
    return topic;
}

std::string LoraGatewayBridge::NodeEntityConfig::getCommandTopic(std::string_view topicPrefix, std::string_view deviceId) const
{
    std::string topic{topicPrefix};
    topic.append(deviceId).append("/").append(name).append("/set");
    return topic;
}