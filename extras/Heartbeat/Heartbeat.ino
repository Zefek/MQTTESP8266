#include "EspDrvV4.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";
const char* mqttUrl = "";
const uint16_t mqttPort = 8883;
const char* mqttId = "heartbeat";
const char* mqttUser = "";
const char* mqttPassword = "";
const char* probeTopic = "test/heartbeat";

const uint8_t sslAuthMode = 2;
const char* sntpServer = "";
const int8_t sntpTimezone = 0;
const uint8_t espRxPin = 4;
const uint8_t espTxPin = 5;
const long espBaud = 57600;

const uint16_t keepAliveSeconds = 10;
const uint16_t idleSeconds = 90;
const uint16_t reportEverySeconds = 10;

void onMessage(char* topic, uint8_t* data, uint16_t length) {}

SoftwareSerial espSerial(espRxPin, espTxPin);
EspDrvV4 drv(&espSerial);
MQTTClient client(&drv, onMessage);

MQTTConnectData connectData = {
  mqttUrl, mqttPort, mqttId, mqttUser, mqttPassword,
  "", 0, false, "",
  true, keepAliveSeconds
};

unsigned long idleStart = 0;
unsigned long lastReport = 0;
uint8_t phase = 0;
bool idleSurvived = false;

void report(const __FlashStringHelper* what, bool ok)
{
  Serial.print(what);
  Serial.println(ok ? F(" OK") : F(" SELHALO"));
}

void setup()
{
  Serial.begin(57600);
  Serial.println(F("--- Heartbeat ---"));
  espSerial.begin(espBaud);
  drv.Init(128, 256);
  drv.SetSecure(true, sslAuthMode);
  drv.SetTimeSource(sntpServer, sntpTimezone);

  if(drv.Connect(ssid, wifiPassword) != WL_CONNECTED)
  {
    Serial.println(F("STOP: WiFi"));
    return;
  }
  report(F("connect"), client.Connect(connectData));
  if(!client.IsConnected())
  {
    return;
  }
  Serial.print(F("keepAlive="));
  Serial.print(keepAliveSeconds);
  Serial.print(F("s, ping ocekavan kazdych "));
  Serial.print(keepAliveSeconds / 2);
  Serial.print(F("s, server odpojuje na "));
  Serial.print((uint16_t)(keepAliveSeconds * 3 / 2));
  Serial.println(F("s ticha"));
  Serial.println(F("faze 1: necinnost"));
  idleStart = millis();
  lastReport = idleStart;
  phase = 1;
}

void loop()
{
  bool alive = client.Loop();

  if(phase == 1)
  {
    unsigned long now = millis();
    if(now - lastReport >= (unsigned long)reportEverySeconds * 1000UL)
    {
      lastReport = now;
      Serial.print(F("  t="));
      Serial.print((now - idleStart) / 1000);
      Serial.print(F("s alive="));
      Serial.println(alive ? F("ano") : F("NE"));
    }
    if(!alive)
    {
      Serial.println(F("faze 1 SELHALA: spojeni zaniklo behem necinnosti"));
      phase = 3;
      return;
    }
    if(now - idleStart >= (unsigned long)idleSeconds * 1000UL)
    {
      idleSurvived = true;
      Serial.println(F("faze 1 OK: prezilo necinnost bez odpojeni"));
      Serial.println(F("faze 2: disconnect a reconnect"));
      client.Disconnect();
      report(F("  po Disconnect je socket zavreny"), !client.IsConnected());
      report(F("  reconnect"), client.Connect(connectData));
      uint8_t payload[8] = { 'h', 'b', 'p', 'r', 'o', 'b', 'e', '!' };
      report(F("  publish po reconnectu"), client.Publish(probeTopic, payload, sizeof(payload), false));
      Serial.println(idleSurvived ? F("--- HOTOVO ---") : F("--- HOTOVO s chybou ---"));
      phase = 3;
    }
  }
}
