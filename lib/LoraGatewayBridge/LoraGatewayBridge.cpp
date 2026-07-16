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
    // Pre-build topic strings
    std::string base{m_config.topicPrefix};
    base.append(m_config.deviceId);
    base.append("/");

    m_availabilityTopic = base + "availability";
    m_rxTopic           = base + "rx";
    m_txTopic           = base + "tx/set";
}

void LoraGatewayBridge::init()
{
    // Subscribe to downlink topic (MQTT → LoRa)
    m_mqttClient.subscribe(m_txTopic, [this](std::string_view payload) {
        onMqttTx(payload);
    });

    // Publish HA discovery after every (re)connect
    m_mqttClient.onConnect([this]() {
        publishHaDiscovery();
    });

    ESP_LOGI(TAG, "Bridge initialised. RX: %s | TX: %s", m_rxTopic.c_str(), m_txTopic.c_str());
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
    // ── RX sensor ──────────────────────────────────────────────────────────────
    {
        std::string discoveryTopic{"homeassistant/sensor/"};
        discoveryTopic.append(m_config.deviceId);
        discoveryTopic.append("/rx/config");

        JsonDocument doc;
        doc["name"]              = "Last LoRa Message";
        doc["unique_id"]         = std::string{m_config.deviceId} + "_rx";
        doc["state_topic"]       = m_rxTopic;
        doc["value_template"]    = "{{ value_json.hex }}";
        doc["availability_topic"]= m_availabilityTopic;
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
        doc["icon"]              = "mdi:radio-tower";

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"][0]= m_config.deviceId;
        device["name"]          = m_config.deviceName;
        device["model"]         = "E32 LoRa Gateway";
        device["manufacturer"]  = "Custom";

        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(discoveryTopic, payload, /*retain=*/true);
        ESP_LOGD(TAG, "HA discovery published: %s", discoveryTopic.c_str());
    }

    // ── Message counter sensor ──────────────────────────────────────────────────
    {
        std::string discoveryTopic{"homeassistant/sensor/"};
        discoveryTopic.append(m_config.deviceId);
        discoveryTopic.append("/rx_count/config");

        JsonDocument doc;
        doc["name"]              = "LoRa Messages Received";
        doc["unique_id"]         = std::string{m_config.deviceId} + "_rx_count";
        doc["state_topic"]       = m_rxTopic;
        doc["value_template"]    = "{{ value_json.count }}";
        doc["availability_topic"]= m_availabilityTopic;
        doc["payload_available"] = "online";
        doc["payload_not_available"] = "offline";
        doc["icon"]              = "mdi:counter";
        doc["state_class"]       = "total_increasing";

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"][0]= m_config.deviceId;
        device["name"]          = m_config.deviceName;
        device["model"]         = "E32 LoRa Gateway";
        device["manufacturer"]  = "Custom";

        std::string payload;
        serializeJson(doc, payload);
        m_mqttClient.publish(discoveryTopic, payload, /*retain=*/true);
    }
}

void LoraGatewayBridge::onLoraReceived(const uint8_t* data, size_t len)
{
    static uint32_t msgCount{0};
    ++msgCount;

    std::string hexStr = toHexString(data, len);
    ESP_LOGI(TAG, "LoRa RX [#%lu] %u byte(s): %s", (unsigned long)msgCount, (unsigned)len, hexStr.c_str());

    JsonDocument doc;
    doc["hex"]   = hexStr;
    doc["len"]   = len;
    doc["count"] = msgCount;

    std::string payload;
    serializeJson(doc, payload);
    m_mqttClient.publish(m_rxTopic, payload, /*retain=*/true);
}

void LoraGatewayBridge::onMqttTx(std::string_view payload)
{
    // Payload is expected to be a hex string, e.g. "55AA0102"
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
