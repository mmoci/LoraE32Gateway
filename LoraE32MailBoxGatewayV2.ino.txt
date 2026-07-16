#include <WiFi.h>
#include <PubSubClient.h>
#include "Config.h"

// E32 Lora board pins
#define M0 21
#define M1 19
#define AUX 27
#define RXD2 17
#define TXD2 16

// Needed only for the first time to program E32 Lora module, set to false after that
#define E32_PROGRAMMING (false)

// Serial connection data messages
#define MAILBOX_FULL_MSG 0x55
#define MAILBOX_EMPTY_MSG 0xAA
#define MAILBOX_ACKNOWLEDGE_MSG 0x25

constexpr int RETRIES = 100;

// WiFi and MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


/*************************************/
/* Main setup() - executed only once */
/*************************************/
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);
  pinMode(AUX, INPUT_PULLUP);

#if E32_PROGRAMMING == true
  // Set LoRa E32 to programming mode (M0 = HIGH, M1 = HIGH)
  Serial.println("Setting Lora E32 programming mode");
  digitalWrite(M0, HIGH);
  digitalWrite(M1, HIGH);
  waitForAux();

   // Configuration array for E32
  byte data[] = { 0xC0, 0x0, 0x1, 0x1A, 0x17, 0x44 };
  for (int i = 0; i < sizeof(data); i++) {
    Serial2.write(data[i]);
    Serial.println(data[i], HEX);
  }
  delay(100);

  // Clear the serial buffer after sending configuration
  while (Serial2.available() > 0) {
    Serial2.read();  // Discard any leftover bytes
  }

  // Set LoRa E32 back to normal mode (M0 = LOW, M1 = LOW)
  digitalWrite(M0, LOW);
  digitalWrite(M1, LOW);
  delay(10);
  Serial.print("Finishing Lora E32 programming mode...");
  waitForAux();
  Serial.println(" DONE ");
#else
   Serial.print("Skipping Lora E32 programming mode...");
#endif

  setup_wifi();
  mqttClient.setServer(MQTT_SERVER.data(), MQTT_PORT);
  mqttClient.setCallback(mqttReceive);

  Serial.println("Setup finished");
}

/***************/
/* Main loop() */
/***************/
void loop() {

  mqttConnect();
  mqttClient.loop();

  if (Serial2.available() > 0) {
    while (Serial2.available() > 0) {
      byte receivedMsg = Serial2.read();
      Serial.printf("Received message is 0x%x\n", receivedMsg);
      handleMessage(receivedMsg);
    }
  }
}

/********************************/
/* Used to setup WiFi connecton */
/********************************/
void setup_wifi() 
{  
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID.data());

  // Set WIFI mode, WIFI_STA - Station (STA) mode is used to get ESP module connected to a WiFi network established by an access point
  WiFi.mode(WIFI_STA);
  WiFi.config(staticIP, gateway, subnet, dns1, dns2); //For statis IP address
  WiFi.begin(WIFI_SSID.data(), WIFI_PASSWORD.data());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

/*************************************************************************************/
/* A function that ensures the E32 module is ready before sending or receiving data. */
/*************************************************************************************/
void waitForAux()
{
  while (digitalRead(AUX) == LOW) {
    delay(10);  // Wait for the AUX pin to go HIGH
  }
  delay(2); // Safety delay to ensure it's ready
}

/************************************/
/* Function handle received message */
/************************************/
void handleMessage(byte receivedMsg)
{
  switch(receivedMsg){
    case MAILBOX_FULL_MSG:
      Serial2.write(MAILBOX_ACKNOWLEDGE_MSG);
      Serial.println("Transmission acknowledged, mailbox FULL");
      mqttPublish("mailbox/state", "full", true);
    break;

    case MAILBOX_EMPTY_MSG:
      Serial2.write(MAILBOX_ACKNOWLEDGE_MSG);
      Serial.println("Transmission acknowledged, mailbox EMPTY");
      mqttPublish("mailbox/state", "empty", true);
    break;

    default:
      mqttPublish("mailbox/state", "empty", true);
    break;
  }
}

/*********************************/
/* Used to receive MQTT messages */
/*********************************/
void mqttReceive(char* topic, byte* payload, unsigned int length)
{
  // Nothing to receive
}

/***************************************************************************/
/* Initial MQTT connection/reconnection, publishing and subscribing topics */
/***************************************************************************/
void mqttConnect() 
{  
  int retries {0};
  const int willQoS {0};
  const boolean willRetain {true};
  std::string willTopic {MQTT_CLIENT_NAME};
  std::string willMessage {"offline"};
  
  while (!mqttClient.connected()) {
    if(retries < RETRIES)
    {
      Serial.println("Attempting MQTT connection...");
      willTopic.append("availability");
      if (mqttClient.connect(MQTT_CLIENT_NAME.data(), MQTT_USERNAME.data(), MQTT_PASSWORD.data(), willTopic.c_str(), willQoS, willRetain, willMessage.c_str())) 
      {
        Serial.println("MQTT Lora Mailbox Gateway state: " + String(mqttClient.state()));

        // Publish MQTT topics
        mqttPublish ("availability", "online", true);

        //Subscribe MQTT topics
        // mqtt_subscribe ("state/set");
      } 
      else 
      {
        // Publish MQTT topic
        mqttPublish ("availability", "offline", true);
        
        Serial.println("Failed to connect to MQTT server");
        Serial.println("MQTT client state: " + String(mqttClient.state()));
        Serial.println("Try again in 5 seconds...");
        retries++;
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    if(retries >= RETRIES)
    {
      // Publish MQTT topic
      mqttPublish ("availability", "offline", true);
  
      Serial.print("Restarting board...");
      ESP.restart();
    }
  } 
}

/***************************************/
/* Method for publishing MQTT messages */
/***************************************/
void mqttPublish (const std::string& topicName, const std::string& topicPayload, boolean retain)
{
  std::string fullTopic {MQTT_CLIENT_NAME};
  fullTopic.append(topicName);

  mqttClient.publish(fullTopic.c_str(), topicPayload.c_str(), retain);
  
  std::string trace {"Publishing topic [" + fullTopic + "] payload: " + topicPayload};
  Serial.println(trace.c_str());  
}
