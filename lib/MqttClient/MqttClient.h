#pragma once

#include <PubSubClient.h>
#include <WiFi.h>
#include <string_view>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>

/**
 * @class MqttClient
 * @brief Thin, reusable MQTT transport layer over PubSubClient.
 *
 * Responsibilities:
 *   - WiFi + MQTT broker connection with automatic reconnection
 *   - Publishing messages with optional broker-side retain
 *   - Subscribing topics with per-topic message callbacks
 *   - Notifying registered listeners on (re)connect
 *
 * NOT responsible for:
 *   - Domain knowledge (topics, payloads, JSON formatting)
 *   - Application state
 *   - WiFi credentials — injected via Config struct
 */
class MqttClient
{
public:
    using MessageCallback = std::function<void(std::string_view payload)>;
    using ConnectCallback = std::function<void()>;

    struct Config
    {
        std::string_view broker;
        int              port{1883};
        std::string_view clientId;
        std::string_view username;
        std::string_view password;
        std::string_view wifiSsid;
        std::string_view wifiPassword;
        std::string_view willTopic;      ///< Availability topic — LWT and online announce
        std::string_view willPayload;    ///< LWT payload (typically "offline")
        std::string_view onlinePayload;  ///< Payload published after connect (typically "online")
        // Optional static IP — if set, DHCP is skipped (faster)
        IPAddress staticIp{};
        IPAddress gateway{};
        IPAddress subnet{};
        IPAddress dns1{};
        IPAddress dns2{};
    };

    explicit MqttClient(const Config& config);

    /**
     * @brief Starts WiFi and configures the MQTT broker endpoint.
     *        Call once from setup().
     */
    void init();

    /**
     * @brief Drives reconnection logic and incoming message dispatching.
     *        Call every loop() iteration.
     */
    void process();

    /**
     * @brief Publish a message to the broker.
     * @param retain  true = broker stores last value for late subscribers.
     */
    void publish(std::string_view topic, std::string_view payload, bool retain = false);

    /**
     * @brief Subscribe to a topic and register a callback.
     *        Safe to call before connect() — replayed on every reconnect.
     */
    void subscribe(std::string_view topic, MessageCallback callback);

    /**
     * @brief Register a callback invoked after every successful (re)connect.
     *        Additive — multiple callers can register without overwriting.
     */
    void onConnect(ConnectCallback callback);

    bool isConnected();

private:
    bool connect();
    bool setupWifi();
    void subscribeAll();
    void dispatchMessage(char* topic, byte* payload, unsigned int length);

    Config       m_config;
    WiFiClient   m_wifiClient{};
    PubSubClient m_mqttClient{};

    std::vector<ConnectCallback>                     m_connectCallbacks{};
    std::unordered_map<std::string, MessageCallback> m_subscriptions{};

    static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS   {10000};
    static constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS{30000};
    static constexpr unsigned long RECONNECT_INTERVAL_MS     { 5000};
};
