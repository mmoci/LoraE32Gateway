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
    ESP_LOGI(TAG, "Node 0x%02X registered: %s (%s)", nodeId, nodeConfig.name.c_str(), nodeConfig.haComponent.c_str());
    m_nodes.emplace(nodeId, std::move(nodeConfig));
}

void LoraGatewayBridge::init()
{
    m_mqttClient.subscribe(m_txTopic, [this](std::string_view payload) {
        handleMqttMsg(payload);
    });

    m_mqttClient.onConnect([this]() {
        publishHaDiscovery();
    });

    ESP_LOGI(TAG, "Bridge initialised. RX: %s | TX: %s | Nodes: %u",
        m_rxTopic.c_str(), m_txTopic.c_str(), (unsigned)m_nodes.size());
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

void LoraGatewayBridge::publishHaDiscovery()
{
    for (const auto& [id, nodeConfig] : m_nodes)
        publishNodeDiscovery(nodeConfig);
}

void LoraGatewayBridge::publishNodeDiscovery(const NodeConfig& nodeConfig)
{
    std::string stateTopic{m_config.topicPrefix};
    stateTopic.append(m_config.deviceId).append("/").append(nodeConfig.name).append("/state");

    std::string discoveryTopic{"homeassistant/"};
    discoveryTopic.append(nodeConfig.haComponent).append("/");
    discoveryTopic.append(m_config.deviceId).append("/").append(nodeConfig.name).append("/config");

    JsonDocument doc;
    doc["name"]                  = nodeConfig.friendlyName.empty() ? nodeConfig.name : nodeConfig.friendlyName;
    doc["unique_id"]             = std::string{m_config.deviceId} + "_" + nodeConfig.name;
    doc["state_topic"]           = stateTopic;
    doc["availability_topic"]    = m_availabilityTopic;
    doc["payload_available"]     = "online";
    doc["payload_not_available"] = "offline";

    if (!nodeConfig.unit.empty())
        doc["unit_of_measurement"] = nodeConfig.unit;
    if (!nodeConfig.deviceClass.empty())
        doc["device_class"] = nodeConfig.deviceClass;

    JsonObject device = doc["device"].to<JsonObject>();
    device["identifiers"][0] = m_config.deviceId;
    device["name"]           = m_config.deviceName;
    device["model"]          = "E32 LoRa Gateway";
    device["manufacturer"]   = "Custom";

    std::string payload;
    serializeJson(doc, payload);
    m_mqttClient.publish(discoveryTopic, payload, /*retain=*/true);
    ESP_LOGD(TAG, "HA discovery: %s", discoveryTopic.c_str());
}

void LoraGatewayBridge::handleLoraMsg(const uint8_t* data, size_t len)
{
    static uint32_t msgCount{0};
    ++msgCount;

    if (len == 0) return;

    // First byte is the node ID — look up a registered handler
    uint8_t nodeId = data[0];
    auto it = m_nodes.find(nodeId);

    if (it != m_nodes.end())
    {
        // ── Registered node: decode and publish to per-node state topic ──────────
        const NodeConfig& nodeConfig = it->second;
        const uint8_t* payload = data + 1;      // everything after the node ID byte
        size_t         payloadLen = len - 1;

        std::string state = nodeConfig.decode(payload, payloadLen);

        std::string stateTopic{m_config.topicPrefix};
        stateTopic.append(m_config.deviceId).append("/").append(nodeConfig.name).append("/state");

        m_mqttClient.publish(stateTopic, state, /*retain=*/true);
        ESP_LOGI(TAG, "Node 0x%02X (%s) → %s", nodeId, nodeConfig.name.c_str(), state.c_str());

        // Send ACK back to sensor if configured (before the sensor retransmits)
        if (nodeConfig.ackByte != 0)
        {
            m_loraModule.send(&nodeConfig.ackByte, 1);
            ESP_LOGD(TAG, "Node 0x%02X ACK sent: 0x%02X", nodeId, nodeConfig.ackByte);
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

void LoraGatewayBridge::handleMqttMsg(std::string_view payload)
{
    if (payload.empty())
    {
        ESP_LOGW(TAG, "MQTT: empty payload — ignoring");
        return;
    }

    if (payload.size() % 2 != 0)
    {
        ESP_LOGE(TAG, "MQTT: odd-length hex string '%.*s' — ignoring", (int)payload.size(), payload.data());
        return;
    }

    std::vector<uint8_t> buf = fromHexString(payload);
    if (buf.empty())
    {
        ESP_LOGE(TAG, "MQTT: invalid hex string '%.*s' — ignoring", (int)payload.size(), payload.data());
        return;
    }

    ESP_LOGI(TAG, "MQTT -> LoRa: %.*s (%u byte(s))", (int)payload.size(), payload.data(), (unsigned)buf.size());
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
