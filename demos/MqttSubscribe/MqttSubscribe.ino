#include "EspDrv.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const char* mqttId = "subscriber";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* subscribeTopic = "test/echo";

void OnMessage(char* topic, uint8_t* payload, uint16_t length);

SoftwareSerial espSerial(4, 5);
EspDrv drv(&espSerial);
MQTTClient client(&drv, OnMessage);

MQTTConnectData connectData = {
  mqttUrl, 1883, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  true, 60
};

void OnMessage(char* topic, uint8_t* payload, uint16_t length)
{
  Serial.print(F("["));
  Serial.print(topic);
  Serial.print(F("] "));
  for(uint16_t i = 0; i < length; i++)
  {
    Serial.write((char)payload[i]);
  }
  Serial.println();
}

void setup()
{
  Serial.begin(57600);
  espSerial.begin(57600);
  drv.Init(128);

  if(drv.Connect(ssid, wifiPassword) == WL_CONNECTED)
  {
    Serial.println(F("WiFi connected"));
    if(client.Connect(connectData))
    {
      Serial.println(F("MQTT connected"));
      if(client.Subscribe(subscribeTopic, 1))
      {
        Serial.print(F("Subscribed to "));
        Serial.println(subscribeTopic);
      }
    }
  }
}

void loop()
{
  client.Loop();
}
