#pragma once

#include <Arduino.h>
#include "Secrets.h"

// ─── E32 LoRa Hardware Pins ────────────────────────────────────────────────
namespace E32Config
{
    constexpr uint8_t  PIN_M0    = 21;   // Mode select 0
    constexpr uint8_t  PIN_M1    = 19;   // Mode select 1
    constexpr uint8_t  PIN_AUX   = 27;   // Busy/ready indicator
    constexpr int8_t   PIN_RXD2  = 17;   // ESP32 RX ← E32 TX
    constexpr int8_t   PIN_TXD2  = 16;   // ESP32 TX → E32 RX
    constexpr uint32_t BAUD_RATE = 9600; // Must match E32 UART setting (byte 3 of config)
}

// ─── Device / MQTT Identity ────────────────────────────────────────────────
namespace GatewayConfig
{
    // Unique identifier for this gateway — used in MQTT topics and HA discovery.
    // Change if you run multiple gateways on the same broker.
    constexpr std::string_view DEVICE_ID    = "lora-gateway";
    constexpr std::string_view DEVICE_NAME  = "LoRa E32 Gateway";

    // Topic root: all gateway topics live under  lora/{DEVICE_ID}/...
    constexpr std::string_view TOPIC_PREFIX = "lora/";

    // MQTT LWT / availability payloads
    constexpr std::string_view PAYLOAD_ONLINE  = "online";
    constexpr std::string_view PAYLOAD_OFFLINE = "offline";
}

// ─── WiFi / MQTT Connection ────────────────────────────────────────────────
// Populated from Secrets.h (not committed to source control).
namespace MqttConfig
{
    constexpr std::string_view BROKER   = MQTT_BROKER;
    constexpr int              PORT     = MQTT_PORT;
    constexpr std::string_view USERNAME = MQTT_USERNAME;
    constexpr std::string_view PASSWORD = MQTT_PASSWORD;
    constexpr std::string_view SSID     = WIFI_SSID;
    constexpr std::string_view WIFI_PWD = WIFI_PASSWORD;
}
