#include "EspDrv.h"
#include <SoftwareSerial.h>

const char* ssid = "";
const char* wifiPassword = "";

SoftwareSerial espSerial(4, 5);
EspDrv drv(&espSerial);

unsigned long lastRssiRead = 0;
const unsigned long rssiInterval = 10000;

void setup()
{
  Serial.begin(57600);
  espSerial.begin(57600);
  drv.Init(128);
  if(drv.Connect(ssid, wifiPassword) == WL_CONNECTED)
  {
    Serial.println(F("WiFi connected"));
  }
  else
  {
    Serial.println(F("WiFi connect failed"));
  }
}

void loop()
{
  drv.Loop();

  unsigned long now = millis();
  if(now - lastRssiRead >= rssiInterval)
  {
    lastRssiRead = now;
    if(drv.GetConnectionStatus() == WL_CONNECTED)
    {
      int8_t rssi = drv.GetRssi();
      if(rssi == 0)
      {
        Serial.println(F("RSSI unavailable"));
      }
      else
      {
        Serial.print(F("RSSI "));
        Serial.print(rssi);
        Serial.println(F(" dBm"));
      }
    }
  }
}
