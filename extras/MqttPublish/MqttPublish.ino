#include "EspDrv.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const char* mqttId = "publisher";
const char* mqttUser = "";
const char* mqttPassword = "";

void noopCallback(char* topic, uint8_t* payload, uint16_t length) {}

SoftwareSerial espSerial(4, 5);
EspDrv drv(&espSerial);
MQTTClient client(&drv, noopCallback);

MQTTConnectData connectData = {
  mqttUrl, 1883, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  true, 60
};

unsigned long lastPublish = 0;
const unsigned long publishInterval = 5000;
unsigned long counter = 0;

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
    }
  }
}

void loop()
{
  client.Loop();

  unsigned long now = millis();
  if(client.IsConnected() && now - lastPublish >= publishInterval)
  {
    lastPublish = now;
    char payload[16];
    snprintf(payload, sizeof(payload), "%lu", counter++);
    if(client.Publish("test/counter", payload))
    {
      Serial.print(F("Published "));
      Serial.println(payload);
    }
  }
}
