#include "EspDrvV4.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const uint16_t mqttPort = 8883;
const char* mqttId = "echoladder";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* echoTopic = "test/echoladder";

const uint8_t sslAuthMode = 2;
const char* sntpServer = "";
const int8_t sntpTimezone = 0;
const uint8_t espRxPin = 4;
const uint8_t espTxPin = 5;
const long espBaud = 57600;
const uint8_t messagesPerSize = 20;
const unsigned long echoTimeout = 3000;
const unsigned long settleDelay = 50;

const uint8_t sizes[] = { 16, 32, 64, 96, 128, 192 };
const uint8_t sizeCount = sizeof(sizes) / sizeof(sizes[0]);

uint8_t received[sizeCount];
uint16_t minRtt[sizeCount];
uint16_t maxRtt[sizeCount];
uint32_t sumRtt[sizeCount];
uint8_t ignoredAt[sizeCount];

uint8_t payload[192];
uint8_t sizeIndex = 0;
uint8_t msgIndex = 0;
uint8_t pendingIndex = 0;
bool awaiting = false;
bool finished = false;
unsigned long sentAt = 0;
unsigned long nextSendAt = 0;
uint8_t ignoredCount = 0;

void onEcho(char* topic, uint8_t* data, uint16_t length);
void onIgnored(uint16_t length);

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
  if(data[0] != sizeIndex || data[1] != pendingIndex)
  {
    return;
  }
  uint16_t rtt = (uint16_t)(millis() - sentAt);
  received[sizeIndex]++;
  sumRtt[sizeIndex] += rtt;
  if(rtt < minRtt[sizeIndex])
  {
    minRtt[sizeIndex] = rtt;
  }
  if(rtt > maxRtt[sizeIndex])
  {
    maxRtt[sizeIndex] = rtt;
  }
  awaiting = false;
}

void onIgnored(uint16_t length)
{
  if(ignoredCount < 255)
  {
    ignoredCount++;
  }
}

void resetResults()
{
  for(uint8_t i = 0; i < sizeCount; i++)
  {
    received[i] = 0;
    minRtt[i] = 0xFFFF;
    maxRtt[i] = 0;
    sumRtt[i] = 0;
    ignoredAt[i] = 0;
  }
}

void reportSize(uint8_t index)
{
  uint16_t avg = received[index] > 0 ? (uint16_t)(sumRtt[index] / received[index]) : 0;
  uint16_t low = received[index] > 0 ? minRtt[index] : 0;
  char line[80];
  snprintf(line, sizeof(line),
           "size=%u recv=%u/%u lost=%u rtt=%u/%u/%u ignored=%u",
           sizes[index], received[index], messagesPerSize,
           (uint8_t)(messagesPerSize - received[index]),
           low, avg, maxRtt[index], ignoredAt[index]);
  Serial.println(line);
}

void setup()
{
  Serial.begin(57600);
  espSerial.begin(espBaud);
  drv.Init(128, 320);
  drv.DataIgnored = onIgnored;
  drv.SetSecure(true, sslAuthMode);
  drv.SetTimeSource(sntpServer, sntpTimezone);
  resetResults();

  for(uint8_t i = 0; i < sizeof(payload); i++)
  {
    payload[i] = 'a' + (i % 26);
  }

  if(drv.Connect(ssid, wifiPassword) == WL_CONNECTED)
  {
    if(client.Connect(connectData))
    {
      client.Subscribe(echoTopic, 1);
      Serial.println(F("echo ladder start"));
    }
  }
}

void loop()
{
  client.Loop();

  if(finished || !client.IsConnected())
  {
    return;
  }

  unsigned long now = millis();

  if(awaiting)
  {
    if(now - sentAt > echoTimeout)
    {
      awaiting = false;
      nextSendAt = now + settleDelay;
    }
    return;
  }

  if(now < nextSendAt)
  {
    return;
  }

  if(msgIndex >= messagesPerSize)
  {
    ignoredAt[sizeIndex] = ignoredCount;
    ignoredCount = 0;
    reportSize(sizeIndex);
    sizeIndex++;
    msgIndex = 0;
    if(sizeIndex >= sizeCount)
    {
      Serial.println(F("echo ladder done"));
      finished = true;
      return;
    }
  }

  payload[0] = sizeIndex;
  payload[1] = msgIndex;
  pendingIndex = msgIndex;
  sentAt = millis();
  awaiting = true;
  if(!client.Publish(echoTopic, payload, sizes[sizeIndex], false))
  {
    awaiting = false;
    nextSendAt = millis() + settleDelay;
  }
  msgIndex++;
}
