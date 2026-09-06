#include "EspDrvV4.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const uint16_t mqttPort = 8883;
const char* mqttId = "loopbudget";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* budgetTopic = "test/loopbudget";

const uint8_t sslAuthMode = 2;
const char* sntpServer = "";
const int8_t sntpTimezone = 0;
const uint8_t espRxPin = 4;
const uint8_t espTxPin = 5;
const long espBaud = 57600;
const uint8_t payloadSize = 16;
const uint8_t messagesPerStep = 10;
const uint8_t lossThreshold = 3;
const uint8_t blockStepMs = 2;
const uint8_t maxBlockMs = 24;
const unsigned long echoTimeout = 3000;
const unsigned long settleDelay = 50;

uint8_t payload[payloadSize];
uint8_t blockMs = 0;
uint8_t msgIndex = 0;
uint8_t pendingIndex = 0;
uint8_t stepReceived = 0;
uint8_t stepTimeouts = 0;
uint8_t firstLossAt = 0;
bool lossSeen = false;
bool awaiting = false;
bool finished = false;
unsigned long sentAt = 0;
unsigned long nextSendAt = 0;

void onEcho(char* topic, uint8_t* data, uint16_t length);

SoftwareSerial espSerial(espRxPin, espTxPin);
EspDrvV4 drv(&espSerial);
MQTTClient client(&drv, onEcho);

MQTTConnectData connectData = {
  mqttUrl, mqttPort, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  true, 60
};

void onEcho(char* topic, uint8_t* data, uint16_t length)
{
  if(!awaiting || length < 2)
  {
    return;
  }
  if(data[0] != blockMs || data[1] != pendingIndex)
  {
    return;
  }
  stepReceived++;
  awaiting = false;
}

void reportStep()
{
  char line[64];
  snprintf(line, sizeof(line), "block=%ums recv=%u/%u lost=%u",
           blockMs, stepReceived, messagesPerStep,
           (uint8_t)(messagesPerStep - stepReceived));
  Serial.println(line);
}

void setup()
{
  Serial.begin(57600);
  espSerial.begin(espBaud);
  drv.Init(128, 256);
  drv.SetSecure(true, sslAuthMode);
  drv.SetTimeSource(sntpServer, sntpTimezone);

  for(uint8_t i = 0; i < payloadSize; i++)
  {
    payload[i] = 'a' + (i % 26);
  }

  if(drv.Connect(ssid, wifiPassword) == WL_CONNECTED)
  {
    if(client.Connect(connectData))
    {
      client.Subscribe(budgetTopic, 1);
      Serial.println(F("loop budget start"));
    }
  }
}

void loop()
{
  if(awaiting)
  {
    drv.Loop();
    unsigned long waited = millis();
    if(waited - sentAt > echoTimeout)
    {
      awaiting = false;
      stepTimeouts++;
      nextSendAt = waited + settleDelay;
    }
    else if(blockMs > 0)
    {
      delay(blockMs);
    }
    return;
  }

  client.Loop();

  if(finished || !client.IsConnected())
  {
    return;
  }

  unsigned long now = millis();

  if(now < nextSendAt)
  {
    return;
  }

  if(msgIndex >= messagesPerStep)
  {
    reportStep();
    if((uint8_t)(messagesPerStep - stepReceived) >= lossThreshold && !lossSeen)
    {
      firstLossAt = blockMs;
      lossSeen = true;
    }
    msgIndex = 0;
    stepReceived = 0;
    stepTimeouts = 0;
    blockMs += blockStepMs;
    if(blockMs > maxBlockMs)
    {
      char line[64];
      if(lossSeen)
      {
        snprintf(line, sizeof(line), "budget exceeded at block=%ums", firstLossAt);
      }
      else
      {
        snprintf(line, sizeof(line), "no sustained loss up to block=%ums", maxBlockMs);
      }
      Serial.println(line);
      finished = true;
      return;
    }
  }

  payload[0] = blockMs;
  payload[1] = msgIndex;
  pendingIndex = msgIndex;
  sentAt = millis();
  awaiting = true;
  if(!client.Publish(budgetTopic, payload, payloadSize, false))
  {
    awaiting = false;
    nextSendAt = millis() + settleDelay;
  }
  msgIndex++;
}
