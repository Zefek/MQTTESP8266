#include "MQTTClient.h"
#include<avr/wdt.h>

static bool MQTTClient::pingOutstanding = false;
static void (*MQTTClient::callback)(char* topic, uint8_t* payload, uint16_t plength) = 0;
static bool MQTTClient::suback = false;
static bool MQTTClient::connack = false;

static void MQTTClient::DataReceived(uint8_t* data, int length)
{
  switch(data[0]&0xF0)
  {
    case MQTTSUBACK:
      MQTTClient::suback = true;
    break;
    case MQTTCONNACK: 
      connack = true;
    break;
    case MQTTPINGRESP: 
      MQTTClient::pingOutstanding = false;
    break;
    case MQTTPUBLISH:
      uint8_t l = data[1];
      uint16_t tl = (data[2]<<8)+data[3];
      memmove(data+3, data+4, tl);
      data[tl+3] = '\0';
      char* topic = data+3;
      uint8_t* payload = data+tl+4;
      callback(topic,payload,l - tl - 2);
    break;
    
  }
}

MQTTClient::MQTTClient(EspDrv *espDriver, void(*callback)(char* topic, uint8_t* payload, uint16_t plength))
{
  this->client = espDriver;
  this->client->DataReceived = &DataReceived;
  this->buffer = new uint8_t[bufferSize];
  this->callback = callback;
}

bool MQTTClient::Connect(MQTTConnectData mqttConnectData)
{
  this->client->TCPConnect(mqttConnectData.url, mqttConnectData.port);
  this->keepAlive = mqttConnectData.keepAlive;
  return this->Login(mqttConnectData);
}

bool MQTTClient::Login(MQTTConnectData mqttConnectData)
{
  connack = false;
  uint16_t length = MQTT_MAX_HEADER_SIZE;
  unsigned int j;

#if MQTT_VERSION == MQTT_VERSION_3_1
  uint8_t d[9] = {0x00,0x06,'M','Q','I','s','d','p', MQTT_VERSION};
#define MQTT_HEADER_VERSION_LENGTH 9
#elif MQTT_VERSION == MQTT_VERSION_3_1_1
  uint8_t d[7] = {0x00,0x04,'M','Q','T','T',MQTT_VERSION};
#define MQTT_HEADER_VERSION_LENGTH 7
#endif
  for (j = 0;j<MQTT_HEADER_VERSION_LENGTH;j++) 
  {
    this->buffer[length++] = d[j];
  }

  uint8_t v;
  if (mqttConnectData.willTopic) 
  {
    v = 0x04|(mqttConnectData.willQos<<3)|(mqttConnectData.willRetain<<5);
  } else 
  {
    v = 0x00;
  }
  if (mqttConnectData.cleanSession)
  {
    v = v|0x02;
  }

  if(mqttConnectData.user != NULL) 
  {
    v = v|0x80;
    if(mqttConnectData.pass != NULL) 
    {
      v = v|(0x80>>1);
    }
  }
  this->buffer[length++] = v;

  this->buffer[length++] = ((this->keepAlive) >> 8);
  this->buffer[length++] = ((this->keepAlive) & 0xFF);

  CHECK_STRING_LENGTH(length,mqttConnectData.id)
  length = WriteString(mqttConnectData.id,this->buffer,length);
  if (mqttConnectData.willTopic) 
  {
    CHECK_STRING_LENGTH(length,mqttConnectData.willTopic)
    length = WriteString(mqttConnectData.willTopic,this->buffer,length);
    CHECK_STRING_LENGTH(length,mqttConnectData.willMessage)
    length = WriteString(mqttConnectData.willMessage,this->buffer,length);
  }

  if(mqttConnectData.user != NULL) 
  {
    CHECK_STRING_LENGTH(length,mqttConnectData.user)
    length = WriteString(mqttConnectData.user,this->buffer,length);
    if(mqttConnectData.pass != NULL) 
    {
      CHECK_STRING_LENGTH(length,mqttConnectData.pass)
      length = WriteString(mqttConnectData.pass,this->buffer,length);
    }
  }
  Write(MQTTCONNECT,this->buffer,length-MQTT_MAX_HEADER_SIZE);
  while(!connack)
  {
    client->Loop();
  }
  isConnected = client->GetClientStatus() == CL_CONNECTED;
  return isConnected;
}

uint16_t MQTTClient::WriteString(const char* string, uint8_t* buf, uint16_t pos)
{
  const char* idp = string;
  uint16_t i = 0;
  pos += 2;
  while (*idp) 
  {
    buf[pos++] = *idp++;
    i++;
  }
  buf[pos-i-2] = (i >> 8);
  buf[pos-i-1] = (i & 0xFF);
  return pos;
}

void MQTTClient::Subscribe(const char* topic) 
{
  Subscribe(topic, 0);
}

void MQTTClient::Subscribe(const char *topic, uint8_t qos)
{
  if(!isConnected)
  {
    return;
  }
  MQTTClient::suback = false;
  size_t topicLength = strnlen(topic, this->bufferSize);
  if (topic == 0) 
  {
    return false;
  }
  if (qos > 1) 
  {
    return false;
  }
  if (this->bufferSize < 9 + topicLength) 
  {
    // Too long
    return false;
  }
  // Leave room in the buffer for header and variable length field
  uint16_t length = MQTT_MAX_HEADER_SIZE;
  nextMsgId++;
  if (nextMsgId == 0) 
  {
    nextMsgId = 1;
  }
  this->buffer[length++] = (nextMsgId >> 8);
  this->buffer[length++] = (nextMsgId & 0xFF);
  length = WriteString((char*)topic, this->buffer,length);
  this->buffer[length++] = qos;
  Write(MQTTSUBSCRIBE|MQTTQOS1,this->buffer,length-MQTT_MAX_HEADER_SIZE);
  while(!MQTTClient::suback)
  {
    client->Loop();
    wdt_reset();
  }
  delay(200);
  client->Loop();
}

void MQTTClient::Publish(const char* topic, const char* payload) 
{
  Publish(topic,(const uint8_t*)payload, payload ? strnlen(payload, this->bufferSize) : 0,false);
}

void MQTTClient::Publish(const char* topic, const char* payload, boolean retained) 
{
  Publish(topic,(const uint8_t*)payload, payload ? strnlen(payload, this->bufferSize) : 0,retained);
}

void MQTTClient::Publish(const char* topic, const uint8_t* payload, unsigned int plength) 
{
  Publish(topic, payload, plength, false);
}

void MQTTClient::Publish(const char* topic, const uint8_t* payload, unsigned int plength, boolean retained)
{
  if(!isConnected)
  {
    return;
  }
  if (this->bufferSize < MQTT_MAX_HEADER_SIZE + 2+strnlen(topic, this->bufferSize) + plength) 
  {
    return false;
  }
  // Leave room in the buffer for header and variable length field
  uint16_t length = MQTT_MAX_HEADER_SIZE;
  length = WriteString(topic,this->buffer,length);

  // Add payload
  uint16_t i;
  for (i=0;i<plength;i++) 
  {
    this->buffer[length++] = payload[i];
  }

  // Write the header
  uint8_t header = MQTTPUBLISH;
  if (retained) 
  {
    header |= 1;
  }
  Write(header,this->buffer,length-MQTT_MAX_HEADER_SIZE);
}

void MQTTClient::Disconnect()
{
  if(!isConnected)
  {
    return;
  }
  buffer[0] = MQTTDISCONNECT;
  buffer[1] = 0;
  this->client->Write(buffer, 2);
}

void MQTTClient::Write(uint8_t header, uint8_t* buf, uint16_t length) 
{
    uint16_t rc;
    uint8_t hlen = BuildHeader(header, buf, length);

#ifdef MQTT_MAX_TRANSFER_SIZE
    uint8_t* writeBuf = buf+(MQTT_MAX_HEADER_SIZE-hlen);
    uint16_t bytesRemaining = length+hlen;  //Match the length type
    uint8_t bytesToWrite;
    boolean result = true;
    while((bytesRemaining > 0) && result) 
    {
      bytesToWrite = (bytesRemaining > MQTT_MAX_TRANSFER_SIZE)?MQTT_MAX_TRANSFER_SIZE:bytesRemaining;
      client->Write(writeBuf,bytesToWrite);
      result = (rc == bytesToWrite);
      bytesRemaining -= rc;
      writeBuf += rc;
    }
#else
    client->Write(buf+(MQTT_MAX_HEADER_SIZE-hlen),length+hlen);
    lastOutActivity = millis();
#endif
}

size_t MQTTClient::BuildHeader(uint8_t header, uint8_t* buf, uint16_t length) 
{
  uint8_t lenBuf[4];
  uint8_t llen = 0;
  uint8_t digit;
  uint8_t pos = 0;
  uint16_t len = length;
  do 
  {
    digit = len  & 127; //digit = len %128
    len >>= 7; //len = len / 128
    if (len > 0) 
    {
      digit |= 0x80;
    }
    lenBuf[pos++] = digit;
    llen++;
  } while(len>0);

  buf[4-llen] = header;
  for (int i=0;i<llen;i++)
  {
    buf[MQTT_MAX_HEADER_SIZE-llen+i] = lenBuf[i];
  }
  return llen+1; // Full header size is variable length bit plus the 1-byte fixed header
}

bool MQTTClient::Loop()
{
  isConnected = IsConnected();
  if(!isConnected)
  {
    return isConnected;
  }
  unsigned long currentMillis = millis();
  if(currentMillis - lastOutActivity >= keepAlive * 1000 && keepAlive > 0)
  {
    if(MQTTClient::pingOutstanding)
    {
      isConnected = false;
      uint8_t* buffer = new uint8_t[2];
      buffer[0] = MQTTDISCONNECT;
      buffer[1] = 0;
      this->client->Write(buffer, 2);
      return isConnected;
    }
    else
    {
      //Send ping request
      MQTTClient::pingOutstanding = true;
      lastOutActivity = currentMillis;
      lastInActivity = currentMillis;
      uint8_t* buffer = new uint8_t[2];
      buffer[0] = MQTTPINGREQ;
      buffer[1] = 0;
      this->client->Write(buffer, 2);
    }
  }
  this->client->Loop();
  return isConnected;
}

bool MQTTClient::IsConnected()
{
  uint8_t status = this->client->GetClientStatus();
  return status == CL_CONNECTED;
}