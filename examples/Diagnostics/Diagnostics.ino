#include "EspDrv.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const char* mqttId = "diag";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* telemetryTopic = "telemetry/diag";

void noopCallback(char* topic, uint8_t* payload, uint16_t length) {}

SoftwareSerial espSerial(4, 5);
EspDrv drv(&espSerial);
MQTTClient client(&drv, noopCallback);

MQTTConnectData connectData = {
  mqttUrl, 1883, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  true, 60
};

unsigned long lastReport = 0;
const unsigned long reportInterval = 30000;

void setup()
{
  Serial.begin(57600);
  espSerial.begin(57600);
  drv.Init(128);

  if(drv.Connect(ssid, wifiPassword) == WL_CONNECTED)
  {
    client.Connect(connectData);
  }
}

void loop()
{
  client.Loop();

  unsigned long now = millis();
  if(client.IsConnected() && now - lastReport >= reportInterval)
  {
    lastReport = now;

    int8_t rssi = drv.GetRssi();
    uint8_t memFail = drv.GetMemAllocFailCount();
    uint8_t tagFail = drv.GetTagRecognitionFailCount();
    unsigned long uptime = now / 1000;

    char payload[64];
    snprintf(payload, sizeof(payload),
             "rssi=%d,memFail=%u,tagFail=%u,uptime=%lu",
             rssi, memFail, tagFail, uptime);

    if(client.Publish(telemetryTopic, payload))
    {
      Serial.println(payload);
    }
  }
}
