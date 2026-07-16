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

void LoraGatewayBridge::registerNode(uint8_t nodeId, NodeDef def)
{
    ESP_LOGI(TAG, "Node 0x%02X registered: %s (%s)", nodeId, def.name.c_str(), def.haComponent.c_str());
    m_nodes.emplace(nodeId, std::move(def));
}

void LoraGatewayBridge::init()
{
    m_mqttClient.subscribe(m_txTopic, [this](std::string_view payload) {
        onMqttTx(payload);
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

    uint8_t buf[RX_BUFFER_SIZE];
    size_t len = m_loraModule.read(buf, sizeof(buf));
    if (len > 0)
        onLoraReceived(buf, len);
}


/******************
* Private methods *
*******************/

void LoraGatewayBridge::publishHaDiscovery()
{
    // ── Registered sensor nodes ─────────────────────────────────────────────────
    for (const auto& [id, def] : m_nodes)
        publishNodeDiscovery(def);

    // ── Generic raw-hex fallback sensor ─────────────────────────────────────────
    {
        std::string discoveryTopic{"homeassistant/sensor/"};
        discoveryTopic.append(m_config.deviceId);
        discoveryTopic.append("/rx/config");

        JsonDocument doc;
        doc["name"]              = "LoRa Raw RX";
        doc["unique_id"]         = std::string{m_config.deviceId} + "_rx";
        doc["state_topic"]       = m_rxTopic;
        doc["value_template"]    = "{{ value_json.hex }}";
        doc["availability_topic"]= m_availabilityTopic;
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
        doc["icon"]              = "mdi:radio-tower";

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"][0] = m_config.deviceId;
        device["name"]           = m_config.deviceName;
        device["model"]          = "E32 LoRa Gateway";
        device["manufacturer"]   = "Custom";

        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(discoveryTopic, payload, /*retain=*/true);
    }
}

void LoraGatewayBridge::publishNodeDiscovery(const NodeDef& def)
{
    std::string stateTopic{m_config.topicPrefix};
    stateTopic.append(m_config.deviceId).append("/").append(def.name).append("/state");

    std::string discoveryTopic{"homeassistant/"};
    discoveryTopic.append(def.haComponent).append("/");
    discoveryTopic.append(m_config.deviceId).append("/").append(def.name).append("/config");

    JsonDocument doc;
    doc["name"]              = def.name;
    doc["unique_id"]         = std::string{m_config.deviceId} + "_" + def.name;
    doc["state_topic"]       = stateTopic;
    doc["availability_topic"]= m_availabilityTopic;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";

    if (!def.unit.empty())
        doc["unit_of_measurement"] = def.unit;
    if (!def.deviceClass.empty())
        doc["device_class"] = def.deviceClass;

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

void LoraGatewayBridge::onLoraReceived(const uint8_t* data, size_t len)
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
        const NodeDef& def = it->second;
        const uint8_t* payload = data + 1;      // everything after the node ID byte
        size_t         payloadLen = len - 1;

        std::string state = def.decode(payload, payloadLen);

        std::string stateTopic{m_config.topicPrefix};
        stateTopic.append(m_config.deviceId).append("/").append(def.name).append("/state");

        m_mqttClient.publish(stateTopic, state, /*retain=*/true);
        ESP_LOGI(TAG, "Node 0x%02X (%s) → %s", nodeId, def.name.c_str(), state.c_str());

        // Send ACK back to sensor if configured (before the sensor retransmits)
        if (def.ackByte != 0)
        {
            m_loraModule.send(&def.ackByte, 1);
            ESP_LOGD(TAG, "Node 0x%02X ACK sent: 0x%02X", nodeId, def.ackByte);
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

void LoraGatewayBridge::onMqttTx(std::string_view payload)
{
    if (payload.empty())
    {
        ESP_LOGW(TAG, "TX: empty payload — ignoring");
        return;
    }

    if (payload.size() % 2 != 0)
    {
        ESP_LOGE(TAG, "TX: odd-length hex string '%.*s' — ignoring", (int)payload.size(), payload.data());
        return;
    }

    uint8_t buf[RX_BUFFER_SIZE];
    size_t  outLen{0};

    if (!fromHexString(payload, buf, outLen))
    {
        ESP_LOGE(TAG, "TX: invalid hex string '%.*s' — ignoring", (int)payload.size(), payload.data());
        return;
    }

    ESP_LOGI(TAG, "TX → LoRa: %.*s (%u byte(s))", (int)payload.size(), payload.data(), (unsigned)outLen);
    m_loraModule.send(buf, outLen);
}

std::string LoraGatewayBridge::toHexString(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        char tmp[3];
        snprintf(tmp, sizeof(tmp), "%02X", data[i]);
        out.append(tmp);
    }
    return out;
}

bool LoraGatewayBridge::fromHexString(std::string_view hex, uint8_t* out, size_t& outLen)
{
    outLen = 0;
    if (hex.size() % 2 != 0)
        return false;

    for (size_t i = 0; i < hex.size(); i += 2)
    {
        char hi = hex[i];
        char lo = hex[i + 1];

        if (!isxdigit(static_cast<unsigned char>(hi)) || !isxdigit(static_cast<unsigned char>(lo)))
            return false;

        auto hexVal = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            return static_cast<uint8_t>(c - 'A' + 10);
        };

        out[outLen++] = static_cast<uint8_t>((hexVal(hi) << 4) | hexVal(lo));
    }
    return true;
}
