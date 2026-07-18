#include <Arduino.h>
#include "Config.h"
#include "MqttClient.h"
#include "E32LoraModule.h"
#include "LoraGatewayBridge.h"
#include "OtaHandler.h"

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
// ─── OTA Handler ─────────────────────────────────────────────────────────────
static OtaHandler otaHandler{OtaHandler::Config{
    .hostname = OtaConfig::HOSTNAME,
    .password = OtaConfig::PASSWORD,
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
    // option = 0x04: transparent mode (bit6=0), FEC on (bit2=1), 20dBm, 250ms wake
    // ⚠ Must match sensor E32 config exactly (same chan=0x17, same air rate)
    const uint8_t e32Cfg[] = { 0xC2, 0x00, 0x01, 0x1A, 0x17, 0x04 };
    loraModule.program(e32Cfg, sizeof(e32Cfg));

    // ─── Register sensor nodes ────────────────────────────────────────────────
    // Each sensor node must send its node ID as the FIRST byte of every LoRa
    // message. The remaining bytes are the payload passed to the decode function.
    //
    // Add a registerNode() call here for every sensor you want to integrate.
    // Unregistered node IDs fall back to raw hex on the generic lora/.../rx topic.

    // Mailbox binary sensor (node ID = 0x01)
    // Sensor sends: [0x01, 0x55] = full  |  [0x01, 0xAA] = empty
    // Gateway sends back 0x25 (ACK) so the sensor state machine advances and stops retransmitting.
    // HA topic: lora/lora-gateway/mailbox/state  →  "ON" / "OFF"
    //
    // ⚠ Sensor firmware change required (LoraE32MailBoxV2):
    //   The sensor currently sends a raw single byte (0x55 or 0xAA).
    //   Add the node ID as the first byte before the message byte:
    //     Before:  Serial.write(transmitMsg);
    //     After:   Serial.write(0x01); Serial.write(transmitMsg);
    bridge.registerNode(0x01, {
        .name         = "mailbox",
        .friendlyName = "Mailbox",
        .haComponent  = "binary_sensor",
        .unit         = "",
        .deviceClass  = "occupancy",
        .decode      = [](const uint8_t* d, size_t len) -> std::string {
            if (len < 1) return "OFF";
            return (d[0] == 0x55) ? "ON" : "OFF";
        },
        .ackByte = 0x25   // MAILBOX_ACKNOWLEDGE_MSG — sensor expects this to stop retransmitting
    });

    // Example: humidity sensor (node ID = 0x02)
    // Sensor sends: [0x02, humidity_uint8, temperature_int8]
    // HA topic:     lora/lora-gateway/humidity/state  →  "65" (percent)
    // bridge.registerNode(0x02, {
    //     .name        = "humidity",
    //     .haComponent = "sensor",
    //     .unit        = "%",
    //     .deviceClass = "humidity",
    //     .decode      = [](const uint8_t* d, size_t len) -> std::string {
    //         if (len < 1) return "0";
    //         return std::to_string(d[0]);
    //     }
    // });

    mqttClient.init();  // connects WiFi, configures broker endpoint
    otaHandler.init();  // starts OTA service (lora-gateway.local)
    bridge.init();      // subscribes TX topic, registers HA discovery on connect
}

void loop()
{
    mqttClient.process(); // handles WiFi/MQTT reconnection + incoming messages
    otaHandler.handle();  // handles OTA firmware update requests
    bridge.process();     // forwards LoRa RX → MQTT
}
