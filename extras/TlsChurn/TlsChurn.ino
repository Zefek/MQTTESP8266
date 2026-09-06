#include "EspDrvV4.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const uint16_t mqttPort = 8883;
const char* mqttId = "tlschurn";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* churnTopic = "test/tlschurn";

const uint8_t sslAuthMode = 2;
const char* sntpServer = "";
const int8_t sntpTimezone = 0;
const uint8_t espRxPin = 4;
const uint8_t espTxPin = 5;
const long espBaud = 57600;

const uint8_t cycleCount = 50;
const unsigned long settleMs = 500;

void onMessage(char* topic, uint8_t* data, uint16_t length) {}

SoftwareSerial espSerial(espRxPin, espTxPin);
EspDrvV4 drv(&espSerial);
MQTTClient client(&drv, onMessage);

MQTTConnectData connectData = {
  mqttUrl, mqttPort, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  true, 60
};

uint16_t minMs = 0xFFFF;
uint16_t maxMs = 0;
uint32_t sumMs = 0;
uint8_t okCount = 0;
uint8_t failCount = 0;
int firstFree = 0;
int lastFree = 0;

int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

void setup()
{
  Serial.begin(57600);
  Serial.println(F("--- TlsChurn ---"));
  espSerial.begin(espBaud);
  drv.Init(128, 256);
  drv.SetSecure(true, sslAuthMode);
  drv.SetTimeSource(sntpServer, sntpTimezone);

  if(drv.Connect(ssid, wifiPassword) != WL_CONNECTED)
  {
    Serial.println(F("STOP: WiFi"));
    return;
  }
  Serial.print(F("cyklu: "));
  Serial.println(cycleCount);

  uint8_t payload[8] = { 'c', 'h', 'u', 'r', 'n', '!', '!', '!' };

  for(uint8_t i = 0; i < cycleCount; i++)
  {
    unsigned long t0 = millis();
    bool connected = client.Connect(connectData);
    uint16_t elapsed = (uint16_t)(millis() - t0);

    bool published = false;
    if(connected)
    {
      published = client.Publish(churnTopic, payload, sizeof(payload), false);
      okCount++;
      sumMs += elapsed;
      if(elapsed < minMs)
      {
        minMs = elapsed;
      }
      if(elapsed > maxMs)
      {
        maxMs = elapsed;
      }
    }
    else
    {
      failCount++;
    }

    client.Disconnect();

    int free = freeRam();
    if(i == 0)
    {
      firstFree = free;
    }
    lastFree = free;

    char line[80];
    snprintf(line, sizeof(line),
             "#%u conn=%u %ums pub=%u free=%d memFail=%u tagFail=%u",
             i, connected ? 1 : 0, elapsed, published ? 1 : 0, free,
             drv.GetMemAllocFailCount(), drv.GetTagRecognitionFailCount());
    Serial.println(line);

    delay(settleMs);
  }

  Serial.println(F("--- souhrn ---"));
  char line[80];
  snprintf(line, sizeof(line), "ok=%u fail=%u connect min/avg/max = %u/%u/%u ms",
           okCount, failCount, okCount ? minMs : 0,
           okCount ? (uint16_t)(sumMs / okCount) : 0, maxMs);
  Serial.println(line);
  // Obe hodnoty ze stejneho mista ve smycce - freeRam() volana odsud by byla
  // v jine hloubce zasobniku a rozdil by nic neznamenal.
  snprintf(line, sizeof(line), "freeRam prvni=%d posledni=%d rozdil=%d",
           firstFree, lastFree, lastFree - firstFree);
  Serial.println(line);
  Serial.println(F("--- hotovo ---"));
}

void loop() {}
