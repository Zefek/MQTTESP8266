#include "EspDrvV4.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const uint16_t mqttPort = 8883;
const char* mqttId = "burst";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* burstTopic = "test/burst";
const char* readyTopic = "test/burst/ready";

const uint8_t sslAuthMode = 2;
const char* sntpServer = "";
const int8_t sntpTimezone = 0;
const uint8_t espRxPin = 4;
const uint8_t espTxPin = 5;
const long espBaud = 57600;

const uint8_t subscribeQos = 0;
const uint8_t qosBufferSize = 16;
const bool useCleanSession = true;
const uint8_t burstSize = 12;
const unsigned long quietMs = 2000;
const unsigned long maxRoundMs = 12000;

const uint8_t blockSteps[] = { 0, 5, 10, 20, 40 };
const uint8_t roundCount = sizeof(blockSteps) / sizeof(blockSteps[0]);

uint16_t receivedMask = 0;
uint8_t receivedCount = 0;
uint8_t ignoredCount = 0;
uint8_t reconnectCount = 0;
uint8_t timeoutCount = 0;
bool needsClose = false;
uint8_t roundIndex = 0;
bool roundActive = false;
bool finished = false;
unsigned long roundStart = 0;
unsigned long lastArrival = 0;

void onBurst(char* topic, uint8_t* data, uint16_t length);
void onIgnored(uint16_t length);
void onTimeout();

SoftwareSerial espSerial(espRxPin, espTxPin);
EspDrvV4 drv(&espSerial);
MQTTClient client(&drv, onBurst, qosBufferSize);

MQTTConnectData connectData = {
  mqttUrl, mqttPort, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  useCleanSession, 60
};

void onBurst(char* topic, uint8_t* data, uint16_t length)
{
  if(!roundActive || length < 1)
  {
    return;
  }
  uint8_t seq = data[0];
  if(seq >= burstSize)
  {
    return;
  }
  uint16_t bit = (uint16_t)1 << seq;
  if((receivedMask & bit) == 0)
  {
    receivedMask |= bit;
    receivedCount++;
  }
  lastArrival = millis();
}

void onTimeout()
{
  // Nekompletni +IPD znamena ztracene bajty. U QoS1 broker ceka na PUBACK,
  // ktery uz nikdy neprijde, a s prefetch 1 tim zastavi cely odber.
  // Jedina cesta ven je spojeni zavrit a nechat broker doručit znovu.
  // Close() se NESMI volat z callbacku, proto jen priznak.
  needsClose = true;
  if(timeoutCount < 255)
  {
    timeoutCount++;
  }
}

void onIgnored(uint16_t length)
{
  if(ignoredCount < 255)
  {
    ignoredCount++;
  }
}

void reportRound()
{
  char missing[40];
  uint8_t pos = 0;
  for(uint8_t i = 0; i < burstSize && pos < sizeof(missing) - 4; i++)
  {
    if((receivedMask & ((uint16_t)1 << i)) == 0)
    {
      pos += snprintf(missing + pos, sizeof(missing) - pos, "%u ", i);
    }
  }
  missing[pos] = '\0';

  char line[80];
  snprintf(line, sizeof(line), "block=%ums recv=%u/%u ign=%u tmo=%u rec=%u chybi: %s",
           blockSteps[roundIndex], receivedCount, burstSize, ignoredCount,
           timeoutCount, reconnectCount, pos == 0 ? "-" : missing);
  Serial.println(line);
}

void startRound()
{
  receivedMask = 0;
  receivedCount = 0;
  ignoredCount = 0;
  reconnectCount = 0;
  timeoutCount = 0;
  needsClose = false;
  roundActive = true;
  roundStart = millis();
  lastArrival = 0;

  char signal[8];
  snprintf(signal, sizeof(signal), "R%u", roundIndex);
  client.Publish(readyTopic, signal);
}

void setup()
{
  Serial.begin(57600);
  Serial.println(F("--- Burst ---"));
  espSerial.begin(espBaud);
  drv.Init(128, 256);
  drv.DataIgnored = onIgnored;
  drv.DataTimeout = onTimeout;
  drv.SetSecure(true, sslAuthMode);
  drv.SetTimeSource(sntpServer, sntpTimezone);

  if(drv.Connect(ssid, wifiPassword) != WL_CONNECTED)
  {
    Serial.println(F("STOP: WiFi"));
    return;
  }
  if(!client.Connect(connectData))
  {
    Serial.println(F("STOP: MQTT"));
    return;
  }
  if(!client.Subscribe(burstTopic, subscribeQos))
  {
    Serial.println(F("STOP: subscribe"));
    return;
  }
  Serial.print(F("burst="));
  Serial.print(burstSize);
  Serial.println(F(" zprav, cekam na driver na PC"));
  startRound();
}

void loop()
{
  if(finished)
  {
    return;
  }

  if(roundActive)
  {
    // U QoS0 staci vycitat - client.Loop() by posilal AT+CIPSTATUS a na
    // half-duplex lince by si tim sketch sam zahazoval prichozi zpravy.
    // U QoS1 ale musi PUBACK odejit, jinak broker s prefetch 1 ceka navzdy.
    if(needsClose)
    {
      needsClose = false;
      drv.Close();
    }
    if(subscribeQos > 0)
    {
      if(!client.Loop())
      {
        if(reconnectCount < 255)
        {
          reconnectCount++;
        }
        if(client.Connect(connectData))
        {
          client.Subscribe(burstTopic, subscribeQos);
        }
      }
    }
    else
    {
      drv.Loop();
    }
    unsigned long now = millis();
    bool quiet = lastArrival != 0 && now - lastArrival > quietMs;
    bool expired = now - roundStart > maxRoundMs;
    if(quiet || expired)
    {
      roundActive = false;
      reportRound();
      roundIndex++;
      if(roundIndex >= roundCount)
      {
        Serial.println(F("--- burst hotovo ---"));
        finished = true;
        return;
      }
      startRound();
      return;
    }
    if(blockSteps[roundIndex] > 0)
    {
      delay(blockSteps[roundIndex]);
    }
    return;
  }

  client.Loop();
}
