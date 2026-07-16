#include <Arduino.h>
#include "Config.h"
#include "MqttClient.h"
#include "E32LoraModule.h"
#include "LoraGatewayBridge.h"

// ─── E32 LoRa Module ────────────────────────────────────────────────────────
static E32LoraModule loraModule{E32LoraModule::Config{
    .pinM0    = E32Config::PIN_M0,
    .pinM1    = E32Config::PIN_M1,
    .pinAux   = E32Config::PIN_AUX,
    .pinRx    = E32Config::PIN_RXD2,
    .pinTx    = E32Config::PIN_TXD2,
    .baudRate = E32Config::BAUD_RATE,
    .serial   = Serial2,
}};

// ─── MQTT Client ────────────────────────────────────────────────────────────
// Availability topic: lora/{deviceId}/availability
static const std::string availabilityTopic{
    std::string{GatewayConfig::TOPIC_PREFIX} + std::string{GatewayConfig::DEVICE_ID} + "/availability"
};

static MqttClient mqttClient{MqttClient::Config{
    .broker        = MqttConfig::BROKER,
    .port          = MqttConfig::PORT,
    .clientId      = GatewayConfig::DEVICE_ID,
    .username      = MqttConfig::USERNAME,
    .password      = MqttConfig::PASSWORD,
    .wifiSsid      = MqttConfig::SSID,
    .wifiPassword  = MqttConfig::WIFI_PWD,
    .willTopic     = availabilityTopic,
    .willPayload   = GatewayConfig::PAYLOAD_OFFLINE,
    .onlinePayload = GatewayConfig::PAYLOAD_ONLINE,
    .staticIp      = NetworkConfig::STATIC_IP,
    .gateway       = NetworkConfig::GATEWAY,
    .subnet        = NetworkConfig::SUBNET,
    .dns1          = NetworkConfig::DNS1,
    .dns2          = NetworkConfig::DNS2,
}};

// ─── Gateway Bridge ─────────────────────────────────────────────────────────
static LoraGatewayBridge bridge{mqttClient, loraModule, LoraGatewayBridge::Config{
    .deviceId    = GatewayConfig::DEVICE_ID,
    .topicPrefix = GatewayConfig::TOPIC_PREFIX,
    .deviceName  = GatewayConfig::DEVICE_NAME,
}};


void setup()
{
    Serial.begin(115200);
    esp_log_level_set("*", ESP_LOG_DEBUG);

    loraModule.init();

    // Apply E32 configuration on every boot using 0xC2 (RAM-only, temporary).
    // 0xC2 is safe to run repeatedly — it does NOT write to the module's flash,
    // so there is no write-endurance concern. Settings are re-applied from firmware
    // each time the ESP32 starts. Adjust bytes to match your channel/address/baud.
    // See E32 datasheet for byte layout: [head, addH, addL, sped, chan, option]
    const uint8_t e32Cfg[] = { 0xC2, 0x00, 0x01, 0x1A, 0x17, 0x44 };
    loraModule.program(e32Cfg, sizeof(e32Cfg));
    mqttClient.init();  // connects WiFi, configures broker endpoint
    bridge.init();      // subscribes TX topic, registers HA discovery on connect
}

void loop()
{
    mqttClient.process(); // handles WiFi/MQTT reconnection + incoming messages
    bridge.process();     // forwards LoRa RX → MQTT
}
