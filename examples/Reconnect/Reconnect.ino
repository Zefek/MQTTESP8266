#include "EspDrv.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const char* mqttId = "resilient";
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

unsigned long currentMillis = 0;
unsigned long lastConnectionTry = 0;
unsigned long connectionTimeout = 0;
const unsigned long maxBackoff = 300000;

void Connect()
{
  if(currentMillis - lastConnectionTry < connectionTimeout)
  {
    return;
  }

  int wifiStatus = drv.GetConnectionStatus();
  bool wifiConnected = wifiStatus == WL_CONNECTED;

  if(wifiStatus == WL_DISCONNECTED || wifiStatus == WL_IDLE_STATUS)
  {
    Serial.println(F("WiFi reconnect"));
    wifiConnected = drv.Connect(ssid, wifiPassword) == WL_CONNECTED;
    lastConnectionTry = currentMillis;
  }

  if(wifiConnected)
  {
    if(!client.IsConnected())
    {
      Serial.println(F("MQTT reconnect"));
      if(client.Connect(connectData))
      {
        connectionTimeout = 0;
      }
      else
      {
        connectionTimeout = min(connectionTimeout * 2 + random(0, 5000), maxBackoff);
      }
      lastConnectionTry = currentMillis;
    }
  }
  else
  {
    connectionTimeout = min(connectionTimeout * 2 + random(5000, 30000), maxBackoff);
    lastConnectionTry = currentMillis;
  }
}

void setup()
{
  Serial.begin(57600);
  espSerial.begin(57600);
  drv.Init(128);
}

void loop()
{
  currentMillis = millis();
  if(!client.Loop())
  {
    Connect();
  }
}
