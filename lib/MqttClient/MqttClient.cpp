#include "MqttClient.h"
#include <Arduino.h>
#include "Logger.h"

static constexpr char TAG[] = "MqttClient";

MqttClient::MqttClient(const Config& config) : m_config{config}, m_mqttClient{m_wifiClient}
{}

void MqttClient::init()
{
    setupWifi();

    m_mqttClient.setServer(m_config.broker.data(), m_config.port);
    m_mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
        dispatchMessage(topic, payload, length);
    });
    m_mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
}

void MqttClient::process()
{
    static unsigned long lastWifiAttempt{0};
    static unsigned long lastReconnectAttempt{0};
    static bool wifiWasConnected{false};

    if (WiFi.status() != WL_CONNECTED)
    {
        wifiWasConnected = false;
        const unsigned long now{millis()};
        if (now - lastWifiAttempt >= WIFI_RECONNECT_INTERVAL_MS)
        {
            lastWifiAttempt = now;
            ESP_LOGW(TAG, "WiFi disconnected — attempting reconnect...");
            WiFi.reconnect();
        }
        return;
    }

    if (!wifiWasConnected)
    {
        wifiWasConnected = true;
        ESP_LOGI(TAG, "WiFi connected. IP: %s", WiFi.localIP().toString().c_str());
    }

    if (!m_mqttClient.connected())
    {
        const unsigned long now{millis()};
        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL_MS)
        {
            lastReconnectAttempt = now;
            if (connect())
                lastReconnectAttempt = 0;
        }
        return;
    }

    m_mqttClient.loop();
}

void MqttClient::publish(std::string_view topic, std::string_view payload, bool retain)
{
    if (!m_mqttClient.connected())
        return;

    bool ok = m_mqttClient.publish(topic.data(), payload.data(), retain);
    if (!ok)
        ESP_LOGE(TAG, "publish() failed for topic: %s", topic.data());
}

void MqttClient::subscribe(std::string_view topic, MessageCallback callback)
{
    m_subscriptions.emplace(std::string{topic}, std::move(callback));

    if (m_mqttClient.connected())
    {
        m_mqttClient.subscribe(topic.data());
        ESP_LOGD(TAG, "Subscribed: %s", topic.data());
    }
}

void MqttClient::onConnect(ConnectCallback callback)
{
    m_connectCallbacks.push_back(std::move(callback));
}

bool MqttClient::isConnected()
{
    return m_mqttClient.connected();
}


/******************
* Private methods *
*******************/

bool MqttClient::connect()
{
    ESP_LOGI(TAG, "Attempting MQTT connection...");

    bool ok = m_mqttClient.connect(
        m_config.clientId.data(),
        m_config.username.data(),
        m_config.password.data(),
        m_config.willTopic.data(),
        /*willQoS*/    0,
        /*willRetain*/ true,
        m_config.willPayload.data()
    );

    if (!ok)
    {
        ESP_LOGE(TAG, "Connection failed, state: %d", m_mqttClient.state());
        return false;
    }

    ESP_LOGI(TAG, "Connected to broker");

    if (!m_config.onlinePayload.empty())
        m_mqttClient.publish(m_config.willTopic.data(), m_config.onlinePayload.data(), /*retain=*/true);

    subscribeAll();

    for (auto& cb : m_connectCallbacks)
        cb();

    return true;
}

bool MqttClient::setupWifi()
{
    ESP_LOGI(TAG, "Connecting to WiFi: %s", m_config.wifiSsid.data());

    WiFi.mode(WIFI_STA);

    if (m_config.staticIp)
        WiFi.config(m_config.staticIp, m_config.gateway, m_config.subnet, m_config.dns1, m_config.dns2);

    Serial.flush();
    WiFi.begin(m_config.wifiSsid.data(), m_config.wifiPassword.data());

    if (m_config.staticIp)
    {
        const unsigned long deadline{millis() + WIFI_CONNECT_TIMEOUT_MS};
        while (WiFi.status() != WL_CONNECTED && millis() < deadline)
        {
            delay(500);
            Serial.print(".");
            Serial.flush();
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            ESP_LOGI(TAG, "WiFi connected. IP: %s", WiFi.localIP().toString().c_str());
            return true;
        }
        ESP_LOGW(TAG, "WiFi unavailable — running without MQTT");
        return false;
    }

    // Dynamic IP: non-blocking — process() polls and reconnects
    return false;
}

void MqttClient::subscribeAll()
{
    for (const auto& [topic, callback] : m_subscriptions)
        m_mqttClient.subscribe(topic.c_str());
}

void MqttClient::dispatchMessage(char* topic, byte* payload, unsigned int length)
{
    std::string_view strTopic{topic};
    std::string_view strPayload{reinterpret_cast<char*>(payload), length};

    ESP_LOGD(TAG, "Received [%s]: %.*s", topic, (int)length, (char*)payload);

    auto it = m_subscriptions.find(std::string{strTopic});
    if (it != m_subscriptions.end())
        it->second(strPayload);
    else
        ESP_LOGW(TAG, "No handler for topic: %s", topic);
}
