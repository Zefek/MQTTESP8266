#include "MQTTClient.h"
#include <avr/wdt.h>

bool MQTTClient::pingOutstanding = false;
void (*MQTTClient::callback)(char* topic, uint8_t* payload, uint16_t plength) = 0;
bool MQTTClient::suback = false;
bool MQTTClient::connack = false;
uint8_t MQTTClient::qosBufferHead = 0;
uint8_t MQTTClient::qosBufferTail = 0;
uint8_t MQTTClient::qosBufferCount = 0;
bool MQTTClient::fullQoSBuffer = false;
uint8_t MQTTClient::qosBufferLength = 16;
uint16_t* MQTTClient::qosBufferPacketIds;

void MQTTClient::DataReceived(uint8_t* data, int length)
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
    {
      // Dekódování Remaining Length (VLQ, 1-4 bajty podle MQTT 3.1.1)
      uint32_t multiplier = 1;
      uint32_t remainingLen = 0;
      uint8_t lenBytes = 0;
      uint8_t idx = 1;
      while(true)
      {
        if(idx >= (uint8_t)length || lenBytes >= 4)
        {
          return;
        }
        uint8_t b = data[idx++];
        remainingLen += (uint32_t)(b & 0x7F) * multiplier;
        lenBytes++;
        if((b & 0x80) == 0)
        {
          break;
        }
        multiplier <<= 7;
      }

      uint8_t varHeaderStart = 1 + lenBytes;
      if(varHeaderStart + 2 > (uint16_t)length)
      {
        return;
      }
      uint16_t topicLen = (data[varHeaderStart] << 8) | data[varHeaderStart + 1];
      if((uint32_t)varHeaderStart + 2 + topicLen > (uint32_t)length)
      {
        return;
      }

      memmove(data + varHeaderStart + 1, data + varHeaderStart + 2, topicLen);
      data[varHeaderStart + 1 + topicLen] = '\0';
      char* topic = (char*)(data + varHeaderStart + 1);

      uint8_t qos = (data[0] >> 1) & 0x03;
      uint16_t payloadOffset = varHeaderStart + 2 + topicLen;

      if (qos > 0)
      {
        if(payloadOffset + 2 > (uint16_t)length)
        {
          return;
        }
        uint16_t packetId = (data[payloadOffset] << 8) | data[payloadOffset + 1];
        if(qosBufferHead == qosBufferTail && qosBufferCount != 0)
        {
          fullQoSBuffer = true;
          return;
        }
        qosBufferPacketIds[qosBufferHead] = packetId;
        qosBufferHead = (qosBufferHead + 1) % qosBufferLength;
        qosBufferCount++;
        payloadOffset += 2;
      }

      uint8_t* payload = data + payloadOffset;
      uint16_t payloadLen = (uint16_t)length - payloadOffset;

      if(callback != nullptr)
      {
        callback(topic, payload, payloadLen);
      }
    }
    break;

  }
}

MQTTClient::MQTTClient(EspDrv *espDriver, void(*callback)(char* topic, uint8_t* payload, uint16_t plength), uint8_t pQosBufferLength = 16)
{
  this->client = espDriver;
  this->client->DataReceived = &DataReceived;
  this->buffer = new uint8_t[bufferSize];
  this->callback = callback;
  fullQoSBuffer = false;
  qosBufferHead = qosBufferTail = 0;
  qosBufferCount = 0;
  qosBufferPacketIds = new uint16_t[pQosBufferLength];
  qosBufferLength = pQosBufferLength;
}

bool MQTTClient::Connect(const MQTTConnectData& mqttConnectData)
{
  this->client->TCPConnect(mqttConnectData.url, mqttConnectData.port);
  this->keepAlive = mqttConnectData.keepAlive;
  fullQoSBuffer = false;
  qosBufferHead = qosBufferTail = 0;
  qosBufferCount = 0;
  nextMsgId = 0;
  return this->Login(mqttConnectData);
}

bool MQTTClient::Login(const MQTTConnectData& mqttConnectData)
{
#if MQTT_VERSION == MQTT_VERSION_3_1
  const uint8_t MQTT_HEADER[MQTT_HEADER_VERSION_LENGTH] = {0x00,0x06,'M','Q','I','s','d','p', MQTT_VERSION};
#elif MQTT_VERSION == MQTT_VERSION_3_1_1
  const uint8_t MQTT_HEADER[MQTT_HEADER_VERSION_LENGTH] = {0x00,0x04,'M','Q','T','T',MQTT_VERSION};
#endif
  uint16_t length = MQTT_MAX_HEADER_SIZE;
  for (uint8_t j = 0; j < MQTT_HEADER_VERSION_LENGTH; j++) 
  {
    this->buffer[length++] = MQTT_HEADER[j];
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
  unsigned long t = millis();
  isConnected = false;
  connack = false;
  while(!connack && millis() - t < 10000)
  {
    wdt_reset();
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

bool MQTTClient::Subscribe(const char* topic)
{
  return Subscribe(topic, 0);
}

bool MQTTClient::Subscribe(const char *topic, uint8_t qos)
{
  if(!isConnected)
  {
    return false;
  }
  if (topic == 0)
  {
    return false;
  }
  if (qos > 1)
  {
    return false;
  }
  size_t topicLength = strnlen(topic, this->bufferSize);
  if (this->bufferSize < 9 + topicLength)
  {
    return false;
  }
  MQTTClient::suback = false;
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
  unsigned long t = millis();
  while(!MQTTClient::suback && millis() - t < 3000)
  {
    client->Loop();
  }
  delay(200);
  client->Loop();
  return MQTTClient::suback;
}

bool MQTTClient::Publish(const char* topic, const char* payload) 
{
  return Publish(topic,(const uint8_t*)payload, payload ? strnlen(payload, this->bufferSize) : 0,false);
}

bool MQTTClient::Publish(const char* topic, const char* payload, boolean retained) 
{
  return Publish(topic,(const uint8_t*)payload, payload ? strnlen(payload, this->bufferSize) : 0,retained);
}

bool MQTTClient::Publish(const char* topic, const uint8_t* payload, unsigned int plength) 
{
  return Publish(topic, payload, plength, false);
}

bool MQTTClient::Publish(const char* topic, const uint8_t* payload, unsigned int plength, boolean retained)
{
  if(!isConnected)
  {
    Serial.println("Not connected");
    return false;
  }
  if (this->bufferSize < MQTT_MAX_HEADER_SIZE + 2+strnlen(topic, this->bufferSize) + plength) 
  {
    Serial.println("Small buffer size");
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
  bool result = Write(header,this->buffer,length-MQTT_MAX_HEADER_SIZE);
  return result;
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

bool MQTTClient::Write(uint8_t header, uint8_t* buf, uint16_t length) 
{
    uint8_t hlen = BuildHeader(header, buf, length);

#ifdef MQTT_MAX_TRANSFER_SIZE
    uint8_t* writeBuf = buf+(MQTT_MAX_HEADER_SIZE-hlen);
    uint16_t bytesRemaining = length+hlen;  //Match the length type
    boolean result = true;
    while((bytesRemaining > 0) && result) 
    {
      uint8_t bytesToWrite = (bytesRemaining > MQTT_MAX_TRANSFER_SIZE) ? MQTT_MAX_TRANSFER_SIZE : bytesRemaining;
      uint16_t rc = client->Write(writeBuf,bytesToWrite);
      result = (rc == bytesToWrite);
      bytesRemaining -= rc;
      writeBuf += rc;
    }
    return true;
#else
    bool result = client->Write(buf+(MQTT_MAX_HEADER_SIZE-hlen),length+hlen);
    lastOutActivity = millis();
    return result;
#endif
}

size_t MQTTClient::BuildHeader(uint8_t header, uint8_t* buf, uint16_t length) 
{
  uint8_t lenBuf[4];
  uint8_t llen = 0;
  uint8_t pos = 0;
  uint16_t len = length;
  do 
  {
    uint8_t digit = len & 127; //digit = len %128
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

void MQTTClient::sendPubAck(uint16_t packetId) 
{
    uint8_t pubackPacket[4];
    pubackPacket[0] = 0x40;                // PUBACK packet type + flags
    pubackPacket[1] = 0x02;                // Remaining length = 2
    pubackPacket[2] = (packetId >> 8) & 0xFF;  // Packet ID MSB
    pubackPacket[3] = packetId & 0xFF;
    client->Write(pubackPacket, 4);
}

bool MQTTClient::Loop()
{
  unsigned long currentMillis = millis();
  IsConnected();
  if(isConnected)
  {
    while(qosBufferHead != qosBufferTail)
    { 
      sendPubAck(qosBufferPacketIds[qosBufferTail]);
      qosBufferTail = (qosBufferTail + 1) % qosBufferLength;
      qosBufferCount--;
    }
    if(fullQoSBuffer)
    {
      this->client->Close();
    }
  }
  if(currentMillis - lastOutActivity >= keepAlive * 1000 && keepAlive > 0 && isConnected)
  {
    if(MQTTClient::pingOutstanding)
    {
      isConnected = false;
      this->Disconnect();
      return isConnected;
    }
    else
    {
      //Send ping request
      MQTTClient::pingOutstanding = true;
      lastOutActivity = currentMillis;
      lastInActivity = currentMillis;
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
  isConnected = status == CL_CONNECTED;
  return isConnected;
}