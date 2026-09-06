#include "EspDrvV4.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const uint16_t mqttPort = 8883;
const char* mqttId = "qosredeliv2";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* dataTopic = "test/qosredelivery";
const char* readyTopic = "test/qosredelivery/ready";

const uint8_t sslAuthMode = 2;
const char* sntpServer = "";
const int8_t sntpTimezone = 0;
const uint8_t espRxPin = 4;
const uint8_t espTxPin = 5;
const long espBaud = 57600;

const uint8_t burstSize = 6;
const uint8_t holdAckAfter = 2;
const unsigned long quietMs = 12000;
const unsigned long maxRunMs = 150000;

uint16_t beforeMask = 0;
uint16_t afterMask = 0;
uint8_t heldSeq = 0xFF;
uint8_t reconnectCount = 0;
uint8_t receivedTotal = 0;
bool needsClose = false;
bool holdDone = false;
bool reconnected = false;
bool finished = false;
unsigned long runStart = 0;
unsigned long lastArrival = 0;

void onData(char* topic, uint8_t* data, uint16_t length);

SoftwareSerial espSerial(espRxPin, espTxPin);
EspDrvV4 drv(&espSerial);
MQTTClient client(&drv, onData);

MQTTConnectData connectData = {
  mqttUrl, mqttPort, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  false, 60
};

void onData(char* topic, uint8_t* data, uint16_t length)
{
  if(length < 1)
  {
    return;
  }
  uint8_t seq = data[0];
  if(seq >= burstSize)
  {
    return;
  }
  if(reconnected)
  {
    afterMask |= (uint16_t)1 << seq;
  }
  else
  {
    beforeMask |= (uint16_t)1 << seq;
  }
  receivedTotal++;
  lastArrival = millis();

  // PUBACK odchazi az z nasledujiciho MQTTClient::Loop(). Kdyz spojeni
  // zavreme drive, tahle zprava zustane nepotvrzena - a broker ji pri
  // perzistentni session musi dodat znovu. To je cela podstata testu.
  if(!reconnected && !holdDone && receivedTotal >= holdAckAfter)
  {
    heldSeq = seq;
    holdDone = true;
    needsClose = true;
  }
}

void reportMask(const __FlashStringHelper* label, uint16_t mask)
{
  Serial.print(label);
  bool any = false;
  for(uint8_t i = 0; i < burstSize; i++)
  {
    if(mask & ((uint16_t)1 << i))
    {
      Serial.print(i);
      Serial.print(' ');
      any = true;
    }
  }
  if(!any)
  {
    Serial.print('-');
  }
  Serial.println();
}

void setup()
{
  Serial.begin(57600);
  Serial.println(F("--- QosRedelivery ---"));
  espSerial.begin(espBaud);
  drv.Init(128, 256);
  drv.SetSecure(true, sslAuthMode);
  drv.SetTimeSource(sntpServer, sntpTimezone);

  if(drv.Connect(ssid, wifiPassword) != WL_CONNECTED)
  {
    Serial.println(F("STOP: WiFi"));
    return;
  }
  if(!client.Connect(connectData))
  {
    Serial.print(F("STOP: MQTT rc="));
    Serial.println(client.GetLastConnackCode());
    return;
  }
  if(!client.Subscribe(dataTopic, 1))
  {
    Serial.println(F("STOP: subscribe"));
    return;
  }
  Serial.print(F("cleanSession=false, qos=1, PUBACK zadrzen u "));
  Serial.print(holdAckAfter);
  Serial.println(F(". zpravy"));

  char signal[8];
  snprintf(signal, sizeof(signal), "R%u", burstSize);
  client.Publish(readyTopic, signal);
  runStart = millis();
}

void loop()
{
  if(finished || runStart == 0)
  {
    return;
  }

  // Musi byt pred client.Loop(), jinak by z nej stihl odejit PUBACK
  // a zprava by uz byla potvrzena.
  if(needsClose)
  {
    needsClose = false;
    Serial.print(F("  zadrzuji PUBACK pro seq="));
    Serial.println(heldSeq);
    drv.Close();
  }

  if(!client.Loop())
  {
    reconnectCount++;
    Serial.println(F("  reconnect"));
    if(client.Connect(connectData))
    {
      client.Subscribe(dataTopic, 1);
      reconnected = true;
      lastArrival = millis();
      Serial.println(F("  pripojeno, cekam na redelivery"));
    }
  }

  unsigned long now = millis();
  bool quiet = lastArrival != 0 && now - lastArrival > quietMs;
  if(quiet || now - runStart > maxRunMs)
  {
    finished = true;
    Serial.println(F("--- vysledek ---"));
    reportMask(F("pred reconnectem: "), beforeMask);
    reportMask(F("po reconnectu:    "), afterMask);
    Serial.print(F("zadrzena seq:     "));
    Serial.println(heldSeq);
    bool heldBack = heldSeq < burstSize && (afterMask & ((uint16_t)1 << heldSeq)) != 0;
    char line[64];
    snprintf(line, sizeof(line), "reconnectu=%u celkem prijato=%u",
             reconnectCount, receivedTotal);
    Serial.println(line);
    Serial.println(heldBack ? F("REDELIVERY POTVRZENA") : F("redelivery neprokazana"));
    Serial.println(F("--- hotovo ---"));
  }
}
