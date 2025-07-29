#include "EspDrv.h"
#include "MQTTClient.h"
#include <SoftwareSerial.h>

#define RELIABILITY_TEST 0
#define RECEIVE_TEST 1

char* ssid = "";
char* wifiPassword = "";
char* mqttUrl = "";
char* mqttUser = "";
char* mqttPassword = "";
char* mqttId = "";

void MQTTMessageReceive(char* topic, uint8_t* payload, uint16_t length);

uint8_t mqttReceivedData[128]; 
double avgSendTime = 0;
int received = 0;
MQTTConnectData mqttConnectData = { mqttUrl, 1883, mqttId, mqttUser, mqttPassword, "", 0, false, "", true, 0x0 }; 

SoftwareSerial serial(4, 5);
EspDrv drv(&serial);
MQTTClient client(&drv, MQTTMessageReceive);
int messageCount = 0;
char data[128];
unsigned long currentMillis = 0;
unsigned long mqttLastConnectionTry = 0;
unsigned long mqttConnectionTimeout = 0;

void Connect()
{

  if(currentMillis - mqttLastConnectionTry < mqttConnectionTimeout)
  {
    return;
  }
  int wifiStatus = drv.GetConnectionStatus();
  Serial.print("Wifi status ");
  Serial.println(wifiStatus);
  bool wifiConnected = wifiStatus == WL_CONNECTED;
  if(wifiStatus == WL_DISCONNECTED || wifiStatus == WL_IDLE_STATUS)
  {
    wifiConnected = drv.Connect(ssid, wifiPassword);
    mqttLastConnectionTry = currentMillis;
  }
  if(wifiConnected)
  {
    bool isConnected = client.IsConnected();
    if(!isConnected)
    {
      Serial.println("Connect");
      if(client.Connect(mqttConnectData))
      {
        Serial.println("Subscribes");
        client.Subscribe("test/echo", 1);
        mqttLastConnectionTry = currentMillis;
        mqttConnectionTimeout = 0;
      }
      else
      {
        mqttLastConnectionTry = currentMillis;
        mqttConnectionTimeout = min(mqttConnectionTimeout * 2 + random(0, 5000), 300000);
      }
    }
  }
  else
  {
    mqttLastConnectionTry = currentMillis;
    mqttConnectionTimeout = min(mqttConnectionTimeout * 2 + random(5000, 30000), 300000);
  }
}

void setup()
{
  Serial.begin(57600);
  serial.begin(57600);
  drv.Init(128);
  drv.Connect(ssid, wifiPassword);
}

#if RELIABILITY_TEST
void MQTTMessageReceive(char* topic, uint8_t* payload, uint16_t length) 
{
  if(strcmp(topic, "test/echo") == 0)
  {
    for(int i = 0; i<length; i++)
    {
      mqttReceivedData[i] = (char)payload[i];
    }
    mqttReceivedData[length] = '\0';
    char* token;
    token = strtok(mqttReceivedData, "|");
    int messageId = atoi(token);
    token = strtok(NULL, "|");
    char* endptr;
    unsigned long sendTime = strtoul(token, &endptr, 0);
    avgSendTime = ((avgSendTime * received) + (millis() - sendTime)) / (double)(received+1);
    received++;
    double reliability = (received / (double)messageCount) * 100;

    Serial.print("Length: ");
    Serial.print(length);
    Serial.print("|");
    Serial.print("Average time: ");
    Serial.print(avgSendTime);
    Serial.print("|");
    Serial.print("Send: ");
    Serial.print(messageCount);
    Serial.print("|");
    Serial.print("Received: ");
    Serial.print(received);
    Serial.print("|");
    Serial.print("Reliability: ");
    Serial.print("|");
    Serial.println(reliability);
  }
}

void loop()
{
  
  currentMillis = millis();
  bool result = client.Loop();
  if(!result)
  {
    Connect();
  }
  if(messageCount < 1000)
  {
    int z = messageCount+1;
    sprintf(data, "%d|%lu ABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890", z, currentMillis);
    bool result = client.Publish("test/echo", data);
    if(result)
    {
      messageCount = messageCount+1;
      Serial.print("Published ");
      Serial.println(messageCount);
    }
  }
}
#endif

#if RECEIVE_TEST
void MQTTMessageReceive(char* topic, uint8_t* payload, uint16_t length) 
{
  if(strcmp(topic, "test/echo") == 0)
  {
    Serial.print("Received: ");
    Serial.println(received++);
  }
}

void loop()
{
  currentMillis = millis();
  bool result = client.Loop();
  if(!result)
  {
    Connect();
  }
}
#endif